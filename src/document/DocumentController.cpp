#include "document/DocumentController.hpp"

#include "document/DocumentLimits.hpp"
#include "document/LayerHierarchy.hpp"
#include "document/SelectionOperation.hpp"
#include "document/SelectionVisibility.hpp"
#include "document/StrokeMask.hpp"
#include "io/DocumentSerializer.hpp"
#include "render/RenderEngine.hpp"

#include <QAction>
#include <QHash>
#include <QPainter>
#include <QPointer>
#include <QScopedValueRollback>
#include <QSet>
#include <QUndoCommand>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace wobble
{

namespace
{

struct HistoryMemoryFootprint
{
    qint64 ownedBytes = 0;
    QHash<QString, qint64> sharedBackings;

    void addOwned(qint64 bytes)
    {
        if (bytes <= 0)
        {
            return;
        }
        if (ownedBytes > std::numeric_limits<qint64>::max() - bytes)
        {
            ownedBytes = std::numeric_limits<qint64>::max();
            return;
        }
        ownedBytes += bytes;
    }

    void addShared(const QString &kind, quint64 identity, qint64 bytes)
    {
        if (identity == 0 || bytes <= 0)
        {
            return;
        }
        const QString key =
            QStringLiteral("%1:%2").arg(kind).arg(identity, 0, 16);
        const auto existing = sharedBackings.constFind(key);
        if (existing == sharedBackings.cend() || *existing < bytes)
        {
            sharedBackings.insert(key, bytes);
        }
    }

    qint64 totalBytes() const
    {
        qint64 total = ownedBytes;
        for (const qint64 bytes : sharedBackings)
        {
            if (total > std::numeric_limits<qint64>::max() - bytes)
            {
                return std::numeric_limits<qint64>::max();
            }
            total += bytes;
        }
        return total;
    }
};

template <typename T>
void accountVectorStorage(HistoryMemoryFootprint &footprint,
    const QVector<T> &items,
    const QString &kind)
{
    if (items.capacity() <= 0 || !items.constData())
    {
        return;
    }
    footprint.addShared(kind,
        reinterpret_cast<quintptr>(items.constData()),
        static_cast<qint64>(items.capacity()) * static_cast<qint64>(sizeof(T)));
}

void accountByteStorage(
    HistoryMemoryFootprint &footprint, const QByteArray &bytes)
{
    if (bytes.capacity() <= 0 || !bytes.constData())
    {
        return;
    }
    footprint.addShared(QStringLiteral("bytes"),
        reinterpret_cast<quintptr>(bytes.constData()),
        bytes.capacity());
}

void accountImageStorage(HistoryMemoryFootprint &footprint, const QImage &image)
{
    if (!image.isNull())
    {
        footprint.addShared(QStringLiteral("image"),
            static_cast<quint64>(image.cacheKey()),
            image.sizeInBytes());
    }
}

void accountPackedMask(HistoryMemoryFootprint &footprint,
    const std::optional<PackedMaskRegion> &mask)
{
    if (mask)
    {
        accountByteStorage(footprint, mask->packedMask);
    }
}

constexpr int wobbleAmountMergeId = 1;
constexpr int animationFramesMergeId = 2;
constexpr int framesPerSecondMergeId = 3;
constexpr int layerOpacityMergeId = 4;

qsizetype totalPointCount(const Document &document)
{
    qsizetype count = 0;
    for (const Layer &layer : document.layers)
    {
        for (const Stroke &stroke : layer.strokes)
        {
            if (stroke.points.size()
                > DocumentLimits::maximumTotalPoints - count)
            {
                return DocumentLimits::maximumTotalPoints + 1;
            }
            count += stroke.points.size();
        }
    }
    return count;
}

qsizetype totalStrokeCount(const Document &document)
{
    qsizetype count = 0;
    for (const Layer &layer : document.layers)
    {
        if (layer.strokes.size() > DocumentLimits::maximumTotalStrokes - count)
        {
            return DocumentLimits::maximumTotalStrokes + 1;
        }
        count += layer.strokes.size();
    }
    return count;
}

QVector<StrokePoint> resampleStrokePoints(
    const QVector<StrokePoint> &source, qsizetype targetCount)
{
    Q_ASSERT(targetCount >= 2 && targetCount < source.size());
    QVector<StrokePoint> result;
    result.reserve(targetCount);
    const qsizetype segmentCount = targetCount - 1;
    const qsizetype lastSourceIndex = source.size() - 1;
    const qsizetype baseStep = lastSourceIndex / segmentCount;
    const qsizetype remainderStep = lastSourceIndex % segmentCount;
    qsizetype sourceIndex = 0;
    qsizetype remainder = 0;
    for (qsizetype index = 0; index < targetCount; ++index)
    {
        result.append(source[sourceIndex]);
        sourceIndex += baseStep;
        remainder += remainderStep;
        if (remainder >= segmentCount)
        {
            ++sourceIndex;
            remainder -= segmentCount;
        }
    }
    return result;
}

qsizetype layerPointCount(const Layer &layer)
{
    qsizetype count = 0;
    for (const Stroke &stroke : layer.strokes)
    {
        if (stroke.points.size() > DocumentLimits::maximumTotalPoints - count)
        {
            return DocumentLimits::maximumTotalPoints + 1;
        }
        count += stroke.points.size();
    }
    return count;
}

bool layerCanProducePixels(const Layer &layer)
{
    return std::any_of(layer.strokes.cbegin(),
        layer.strokes.cend(),
        [](const Stroke &stroke)
        {
            return stroke.mode == StrokeMode::Paint
                   || stroke.mode == StrokeMode::Fill;
        });
}

bool isValidInputStrokePoint(const StrokePoint &point, const QSize &size)
{
    return std::isfinite(point.position.x())
           && std::isfinite(point.position.y()) && std::isfinite(point.pressure)
           && point.position.x() >= 0.0 && point.position.y() >= 0.0
           && point.position.x() <= size.width()
           && point.position.y() <= size.height() && point.pressure >= 0.0
           && point.pressure <= 1.0;
}

bool isValidStoredStrokePoint(const StrokePoint &point)
{
    return std::isfinite(point.position.x())
           && std::isfinite(point.position.y())
           && std::abs(point.position.x())
                  <= DocumentLimits::maximumStoredCoordinateMagnitude
           && std::abs(point.position.y())
                  <= DocumentLimits::maximumStoredCoordinateMagnitude
           && std::isfinite(point.pressure) && point.pressure >= 0.0
           && point.pressure <= 1.0;
}

QString visibilityCacheKey(const Stroke &stroke)
{
    const qint64 maskKey =
        stroke.clipMask.isNull() ? 0 : stroke.clipMask.cacheKey();
    if (!stroke.visibilityClip)
    {
        return QString::number(maskKey);
    }
    const QRect &rect = *stroke.visibilityClip;
    return QStringLiteral("%1:%2,%3,%4,%5")
        .arg(maskKey)
        .arg(rect.x())
        .arg(rect.y())
        .arg(rect.width())
        .arg(rect.height());
}

quint64 distinctClipMaskBytes(const Document &document)
{
    QSet<qint64> seen;
    QSet<quintptr> seenPackedBackings;
    quint64 bytes = 0;
    for (const Layer &layer : document.layers)
    {
        for (const Stroke &stroke : layer.strokes)
        {
            for (const QImage *mask : {&stroke.clipMask, &stroke.fillMask})
            {
                if (mask->isNull() || seen.contains(mask->cacheKey()))
                {
                    continue;
                }
                seen.insert(mask->cacheKey());
                const quint64 maskBytes = mask->sizeInBytes();
                if (maskBytes
                    > DocumentLimits::maximumDistinctClipMaskBytes - bytes)
                {
                    return DocumentLimits::maximumDistinctClipMaskBytes + 1;
                }
                bytes += maskBytes;
            }
            const auto registerPacked = [&bytes, &seenPackedBackings](
                                            const QByteArray &packed)
            {
                const quintptr backing =
                    reinterpret_cast<quintptr>(packed.constData());
                if (seenPackedBackings.contains(backing))
                {
                    return true;
                }
                const quint64 packedBytes = static_cast<quint64>(packed.size());
                if (packedBytes
                    > DocumentLimits::maximumDistinctClipMaskBytes - bytes)
                {
                    return false;
                }
                seenPackedBackings.insert(backing);
                bytes += packedBytes;
                return true;
            };
            if ((stroke.pixelSelectionOp
                    && !registerPacked(stroke.pixelSelectionOp->packedMask)))
            {
                return DocumentLimits::maximumDistinctClipMaskBytes + 1;
            }
        }
    }
    return bytes;
}

std::optional<Stroke> selectionOperationStroke(const QImage &selectionMask,
    const QTransform &transform,
    bool clearSource,
    bool drawDestination)
{
    const std::optional<PixelSelectionOp> operation = makePixelSelectionOp(
        selectionMask, transform, clearSource, drawDestination);
    if (!operation)
    {
        return std::nullopt;
    }
    Stroke stroke;
    stroke.mode = StrokeMode::PixelSelection;
    stroke.points.clear();
    stroke.pixelSelectionOp = *operation;
    return stroke;
}

}

struct DocumentController::HistoryEffects
{
    struct CanvasResize
    {
        QSize beforeSize;
        QSize afterSize;
        QTransform forwardTransform;
        QTransform reverseTransform;
    };

    struct StrokeTransform
    {
        QUuid layerId;
        QVector<QUuid> strokeIds;
        QTransform forwardTransform;
        QTransform reverseTransform;
    };

    struct StrokeDuplicate
    {
        QUuid layerId;
        QVector<QUuid> sourceIds;
        QVector<QUuid> duplicateIds;
        QPointF delta;
    };

    struct SelectionOverlay
    {
        QUuid layerId;
        QVector<QUuid> beforeIds;
        QVector<QUuid> afterIds;
        std::optional<PackedMaskRegion> beforeMask;
        std::optional<PackedMaskRegion> afterMask;
    };

    struct StrokePresence
    {
        QUuid layerId;
        QUuid strokeId;
        std::optional<PackedMaskRegion> clipMask;
    };

    struct LayerThumbnail
    {
        QUuid layerId;
    };

    struct LayerThumbnailsReset
    {
    };

    struct ActiveLayer
    {
    };

    struct SelectionState
    {
        QUuid layerId;
        std::optional<PackedMaskRegion> mask;
    };

    struct SelectionStateTransition
    {
        SelectionState before;
        SelectionState after;
    };

    using BeforeEvent = std::variant<CanvasResize,
        StrokeTransform,
        StrokeDuplicate,
        SelectionOverlay,
        StrokePresence>;
    using AfterEvent =
        std::variant<LayerThumbnail, LayerThumbnailsReset, ActiveLayer>;

    QVector<BeforeEvent> beforeDocumentChanged;
    QVector<AfterEvent> afterDocumentChanged;
    std::optional<SelectionStateTransition> selectionState;

    static bool sameSelectionState(
        const SelectionState &left, const SelectionState &right)
    {
        return left.layerId == right.layerId && left.mask == right.mask;
    }

    bool isEmpty() const
    {
        return beforeDocumentChanged.isEmpty() && afterDocumentChanged.isEmpty()
               && !selectionState;
    }

    bool hasDocumentEffects() const
    {
        return !beforeDocumentChanged.isEmpty()
               || !afterDocumentChanged.isEmpty();
    }

    bool hasSelectionTransition() const
    {
        return selectionState
               && !sameSelectionState(
                   selectionState->before, selectionState->after);
    }

    static QVector<QUuid> owningIds(const QVector<QUuid> &source)
    {
        QVector<QUuid> copy;
        copy.reserve(source.size());
        for (const QUuid &id : source)
        {
            copy.append(id);
        }
        return copy;
    }

    static std::optional<PackedMaskRegion> owningMask(
        const std::optional<PackedMaskRegion> &source)
    {
        if (!source)
        {
            return std::nullopt;
        }
        PackedMaskRegion copy = *source;
        copy.packedMask = QByteArray(
            source->packedMask.constData(), source->packedMask.size());
        return copy;
    }

    HistoryEffects frozenCopy() const
    {
        HistoryEffects frozen = *this;
        for (BeforeEvent &event : frozen.beforeDocumentChanged)
        {
            std::visit(
                [](auto &value)
                {
                    using T = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<T, StrokeTransform>)
                    {
                        value.strokeIds = owningIds(value.strokeIds);
                    }
                    else if constexpr (std::is_same_v<T, StrokeDuplicate>)
                    {
                        value.sourceIds = owningIds(value.sourceIds);
                        value.duplicateIds = owningIds(value.duplicateIds);
                    }
                    else if constexpr (std::is_same_v<T, SelectionOverlay>)
                    {
                        value.beforeIds = owningIds(value.beforeIds);
                        value.afterIds = owningIds(value.afterIds);
                        value.beforeMask = owningMask(value.beforeMask);
                        value.afterMask = owningMask(value.afterMask);
                    }
                    else if constexpr (std::is_same_v<T, StrokePresence>)
                    {
                        value.clipMask = owningMask(value.clipMask);
                    }
                },
                event);
        }
        if (frozen.selectionState)
        {
            frozen.selectionState->before.mask =
                owningMask(frozen.selectionState->before.mask);
            frozen.selectionState->after.mask =
                owningMask(frozen.selectionState->after.mask);
        }
        return frozen;
    }

    void append(const HistoryEffects &later)
    {
        beforeDocumentChanged += later.beforeDocumentChanged;
        afterDocumentChanged += later.afterDocumentChanged;
        if (later.selectionState)
        {
            if (!selectionState)
            {
                selectionState = later.selectionState;
            }
            else
            {
                selectionState->after = later.selectionState->after;
            }
            if (!hasSelectionTransition())
            {
                selectionState.reset();
            }
        }
    }

    void discardDocumentEffects()
    {
        beforeDocumentChanged.clear();
        afterDocumentChanged.clear();
    }

    void accountStorage(HistoryMemoryFootprint &footprint) const
    {
        footprint.addOwned(
            static_cast<qint64>(beforeDocumentChanged.capacity())
                * static_cast<qint64>(sizeof(BeforeEvent))
            + static_cast<qint64>(afterDocumentChanged.capacity())
                  * static_cast<qint64>(sizeof(AfterEvent))
            + static_cast<qint64>(sizeof(HistoryEffects)));
        for (const BeforeEvent &event : beforeDocumentChanged)
        {
            std::visit(
                [&footprint](const auto &value)
                {
                    using T = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<T, StrokeTransform>)
                    {
                        accountVectorStorage(footprint,
                            value.strokeIds,
                            QStringLiteral("effect-transform-ids"));
                    }
                    else if constexpr (std::is_same_v<T, StrokeDuplicate>)
                    {
                        accountVectorStorage(footprint,
                            value.sourceIds,
                            QStringLiteral("effect-source-ids"));
                        accountVectorStorage(footprint,
                            value.duplicateIds,
                            QStringLiteral("effect-duplicate-ids"));
                    }
                    else if constexpr (std::is_same_v<T, SelectionOverlay>)
                    {
                        accountVectorStorage(footprint,
                            value.beforeIds,
                            QStringLiteral("effect-before-ids"));
                        accountVectorStorage(footprint,
                            value.afterIds,
                            QStringLiteral("effect-after-ids"));
                        accountPackedMask(footprint, value.beforeMask);
                        accountPackedMask(footprint, value.afterMask);
                    }
                    else if constexpr (std::is_same_v<T, StrokePresence>)
                    {
                        accountPackedMask(footprint, value.clipMask);
                    }
                },
                event);
        }
        if (selectionState)
        {
            accountPackedMask(footprint, selectionState->before.mask);
            accountPackedMask(footprint, selectionState->after.mask);
        }
    }
};

struct DocumentController::MacroTransaction
{
    QString text;
    int depth = 1;
    bool failed = false;
    PreparedState startState;
    PreparedState workingState;
    HistoryEffects effects;
};

struct DocumentController::DocumentDelta
{
    template <typename T> struct ValueChange
    {
        T before;
        T after;
    };

    struct IndexedStroke
    {
        int index = -1;
        Stroke stroke;
    };

    struct ReplacedStroke
    {
        QUuid id;
        Stroke before;
        Stroke after;
    };

    struct StrokeSequenceDelta
    {
        QVector<IndexedStroke> removed;
        QVector<IndexedStroke> added;
        QVector<ReplacedStroke> replaced;
        QVector<QUuid> beforeOrder;
        QVector<QUuid> afterOrder;

        bool isEmpty() const
        {
            return removed.isEmpty() && added.isEmpty() && replaced.isEmpty()
                   && beforeOrder.isEmpty();
        }

        qsizetype retainedStrokeCount() const
        {
            return removed.size() + added.size() + replaced.size() * 2;
        }
    };

    struct LayerChange
    {
        QUuid id;
        std::optional<ValueChange<QString>> name;
        std::optional<ValueChange<bool>> visible;
        std::optional<ValueChange<bool>> reference;
        std::optional<ValueChange<qreal>> opacity;
        std::optional<ValueChange<LayerBlendMode>> blendMode;
        std::optional<ValueChange<QUuid>> parentGroupId;
        std::optional<ValueChange<bool>> clipToLayerBelow;
        std::optional<ValueChange<QSize>> initialCanvasSize;
        StrokeSequenceDelta strokes;

        bool isEmpty() const
        {
            return !name && !visible && !reference && !opacity && !blendMode
                   && !parentGroupId && !clipToLayerBelow && !initialCanvasSize
                   && strokes.isEmpty();
        }
    };

    struct IndexedLayer
    {
        int index = -1;
        Layer layer;
    };

    std::optional<ValueChange<QSize>> size;
    std::optional<ValueChange<QColor>> background;
    std::optional<ValueChange<int>> animationFrames;
    std::optional<ValueChange<qreal>> framesPerSecond;
    std::optional<ValueChange<qreal>> wobbleAmount;
    std::optional<ValueChange<QUuid>> activeLayerId;
    QVector<IndexedLayer> removedLayers;
    QVector<IndexedLayer> addedLayers;
    QVector<LayerChange> changedLayers;
    QVector<QUuid> beforeLayerOrder;
    QVector<QUuid> afterLayerOrder;

    static bool samePointBacking(
        const QVector<StrokePoint> &left, const QVector<StrokePoint> &right)
    {
        return left.size() == right.size()
               && (left.isEmpty() || left.constData() == right.constData());
    }

    static bool sameStrokeVectorBacking(
        const QVector<Stroke> &left, const QVector<Stroke> &right)
    {
        return left.size() == right.size()
               && (left.isEmpty() || left.constData() == right.constData());
    }

    static bool sameByteBacking(const QByteArray &left, const QByteArray &right)
    {
        return left.size() == right.size()
               && (left.isEmpty() || left.constData() == right.constData());
    }

    static bool sameImageBacking(const QImage &left, const QImage &right)
    {
        if (left.isNull() || right.isNull())
        {
            return left.isNull() && right.isNull();
        }
        return left.cacheKey() == right.cacheKey()
               && left.size() == right.size()
               && left.format() == right.format();
    }

    static bool samePixelSelectionOperation(
        const std::optional<PixelSelectionOp> &left,
        const std::optional<PixelSelectionOp> &right)
    {
        if (!left || !right)
        {
            return !left && !right;
        }
        return left->canvasSize == right->canvasSize
               && left->sourceBounds == right->sourceBounds
               && sameByteBacking(left->packedMask, right->packedMask)
               && left->transform == right->transform
               && left->sampling == right->sampling
               && left->clearSource == right->clearSource
               && left->drawDestination == right->drawDestination;
    }

    static bool sameStroke(const Stroke &left, const Stroke &right)
    {
        return left.id == right.id && left.seed == right.seed
               && left.mode == right.mode && left.color == right.color
               && left.width == right.width && left.brush == right.brush
               && samePointBacking(left.points, right.points)
               && left.visibilityClip == right.visibilityClip
               && sameImageBacking(left.clipMask, right.clipMask)
               && sameImageBacking(left.fillMask, right.fillMask)
               && samePixelSelectionOperation(
                   left.pixelSelectionOp, right.pixelSelectionOp)
               && left.reframeOp == right.reframeOp;
    }

    template <typename T>
    static void recordChange(const T &before,
        const T &after,
        std::optional<ValueChange<T>> &destination)
    {
        if (before != after)
        {
            destination = ValueChange<T>{before, after};
        }
    }

    static QVector<QUuid> strokeIds(const QVector<Stroke> &strokes)
    {
        QVector<QUuid> ids;
        ids.reserve(strokes.size());
        for (const Stroke &stroke : strokes)
        {
            ids.append(stroke.id);
        }
        return ids;
    }

    static QVector<QUuid> layerIds(const QVector<Layer> &layers)
    {
        QVector<QUuid> ids;
        ids.reserve(layers.size());
        for (const Layer &layer : layers)
        {
            ids.append(layer.id);
        }
        return ids;
    }

    static StrokeSequenceDelta betweenStrokes(
        const QVector<Stroke> &before, const QVector<Stroke> &after)
    {
        if (sameStrokeVectorBacking(before, after))
        {
            return {};
        }

        // Appending is the dominant drawing path, while equal-position
        // replacement covers transforms.  Avoid two large UUID hash tables
        // when the common prefix already proves that no sequence matching is
        // needed.
        const int commonPrefixSize = std::min(before.size(), after.size());
        StrokeSequenceDelta aligned;
        bool commonPrefixMatches = true;
        for (int index = 0; index < commonPrefixSize; ++index)
        {
            if (before[index].id != after[index].id)
            {
                commonPrefixMatches = false;
                break;
            }
            if (!sameStroke(before[index], after[index]))
            {
                aligned.replaced.append(
                    {before[index].id, before[index], after[index]});
            }
        }
        if (commonPrefixMatches)
        {
            for (int index = commonPrefixSize; index < before.size(); ++index)
            {
                aligned.removed.append({index, before[index]});
            }
            for (int index = commonPrefixSize; index < after.size(); ++index)
            {
                aligned.added.append({index, after[index]});
            }
            return aligned;
        }

        StrokeSequenceDelta delta;
        QHash<QUuid, int> beforeIndexes;
        QHash<QUuid, int> afterIndexes;
        beforeIndexes.reserve(before.size());
        afterIndexes.reserve(after.size());
        for (int index = 0; index < before.size(); ++index)
        {
            beforeIndexes.insert(before[index].id, index);
        }
        for (int index = 0; index < after.size(); ++index)
        {
            afterIndexes.insert(after[index].id, index);
        }

        QVector<QUuid> commonBefore;
        QVector<QUuid> commonAfter;
        commonBefore.reserve(std::min(before.size(), after.size()));
        commonAfter.reserve(std::min(before.size(), after.size()));
        for (int index = 0; index < before.size(); ++index)
        {
            const Stroke &stroke = before[index];
            const auto afterIndex = afterIndexes.constFind(stroke.id);
            if (afterIndex == afterIndexes.cend())
            {
                delta.removed.append({index, stroke});
                continue;
            }
            commonBefore.append(stroke.id);
            const Stroke &afterStroke = after[*afterIndex];
            if (!sameStroke(stroke, afterStroke))
            {
                delta.replaced.append({stroke.id, stroke, afterStroke});
            }
        }
        for (int index = 0; index < after.size(); ++index)
        {
            const Stroke &stroke = after[index];
            if (!beforeIndexes.contains(stroke.id))
            {
                delta.added.append({index, stroke});
            }
            else
            {
                commonAfter.append(stroke.id);
            }
        }
        if (commonBefore != commonAfter)
        {
            delta.beforeOrder = strokeIds(before);
            delta.afterOrder = strokeIds(after);
        }
        return delta;
    }

    static DocumentDelta between(const Document &before, const Document &after)
    {
        DocumentDelta delta;
        recordChange(before.size, after.size, delta.size);
        recordChange(before.background, after.background, delta.background);
        recordChange(before.animationFrames,
            after.animationFrames,
            delta.animationFrames);
        recordChange(before.framesPerSecond,
            after.framesPerSecond,
            delta.framesPerSecond);
        recordChange(
            before.wobbleAmount, after.wobbleAmount, delta.wobbleAmount);
        recordChange(
            before.activeLayerId, after.activeLayerId, delta.activeLayerId);

        QHash<QUuid, int> beforeIndexes;
        QHash<QUuid, int> afterIndexes;
        beforeIndexes.reserve(before.layers.size());
        afterIndexes.reserve(after.layers.size());
        for (int index = 0; index < before.layers.size(); ++index)
        {
            beforeIndexes.insert(before.layers[index].id, index);
        }
        for (int index = 0; index < after.layers.size(); ++index)
        {
            afterIndexes.insert(after.layers[index].id, index);
        }

        QVector<QUuid> commonBefore;
        QVector<QUuid> commonAfter;
        commonBefore.reserve(
            std::min(before.layers.size(), after.layers.size()));
        commonAfter.reserve(
            std::min(before.layers.size(), after.layers.size()));
        for (int index = 0; index < before.layers.size(); ++index)
        {
            const Layer &layer = before.layers[index];
            const auto afterIndex = afterIndexes.constFind(layer.id);
            if (afterIndex == afterIndexes.cend())
            {
                delta.removedLayers.append({index, layer});
                continue;
            }
            commonBefore.append(layer.id);
            const Layer &afterLayer = after.layers[*afterIndex];
            LayerChange change;
            change.id = layer.id;
            recordChange(layer.name, afterLayer.name, change.name);
            recordChange(layer.visible, afterLayer.visible, change.visible);
            recordChange(
                layer.reference, afterLayer.reference, change.reference);
            recordChange(layer.opacity, afterLayer.opacity, change.opacity);
            recordChange(
                layer.blendMode, afterLayer.blendMode, change.blendMode);
            recordChange(layer.parentGroupId,
                afterLayer.parentGroupId,
                change.parentGroupId);
            recordChange(layer.clipToLayerBelow,
                afterLayer.clipToLayerBelow,
                change.clipToLayerBelow);
            recordChange(layer.initialCanvasSize,
                afterLayer.initialCanvasSize,
                change.initialCanvasSize);
            if (!sameStrokeVectorBacking(layer.strokes, afterLayer.strokes))
            {
                change.strokes =
                    betweenStrokes(layer.strokes, afterLayer.strokes);
            }
            if (!change.isEmpty())
            {
                delta.changedLayers.append(std::move(change));
            }
        }
        for (int index = 0; index < after.layers.size(); ++index)
        {
            const Layer &layer = after.layers[index];
            if (!beforeIndexes.contains(layer.id))
            {
                delta.addedLayers.append({index, layer});
            }
            else
            {
                commonAfter.append(layer.id);
            }
        }
        if (commonBefore != commonAfter)
        {
            delta.beforeLayerOrder = layerIds(before.layers);
            delta.afterLayerOrder = layerIds(after.layers);
        }
        return delta;
    }

    static DocumentDelta appendedStroke(
        const Document &before, const Document &after, const QUuid &layerId)
    {
        DocumentDelta delta;
        if (before.size != after.size || before.background != after.background
            || before.animationFrames != after.animationFrames
            || before.framesPerSecond != after.framesPerSecond
            || before.wobbleAmount != after.wobbleAmount
            || before.activeLayerId != after.activeLayerId
            || before.layers.size() != after.layers.size())
        {
            return delta;
        }
        for (int index = 0; index < before.layers.size(); ++index)
        {
            const Layer &beforeLayer = before.layers[index];
            const Layer &afterLayer = after.layers[index];
            if (beforeLayer.id != afterLayer.id
                || beforeLayer.name != afterLayer.name
                || beforeLayer.kind != afterLayer.kind
                || beforeLayer.parentGroupId != afterLayer.parentGroupId
                || beforeLayer.clipToLayerBelow != afterLayer.clipToLayerBelow
                || beforeLayer.visible != afterLayer.visible
                || beforeLayer.reference != afterLayer.reference
                || beforeLayer.opacity != afterLayer.opacity
                || beforeLayer.blendMode != afterLayer.blendMode
                || beforeLayer.initialCanvasSize
                       != afterLayer.initialCanvasSize)
            {
                return {};
            }
            if (beforeLayer.id == layerId)
            {
                if (afterLayer.strokes.size() != beforeLayer.strokes.size() + 1)
                {
                    return {};
                }
                LayerChange change;
                change.id = layerId;
                change.strokes.added.append(
                    {static_cast<int>(beforeLayer.strokes.size()),
                        afterLayer.strokes.constLast()});
                delta.changedLayers.append(std::move(change));
            }
            else if (!sameStrokeVectorBacking(
                         beforeLayer.strokes, afterLayer.strokes))
            {
                return {};
            }
        }
        return delta;
    }

    bool isEmpty() const
    {
        return !size && !background && !animationFrames && !framesPerSecond
               && !wobbleAmount && !activeLayerId && removedLayers.isEmpty()
               && addedLayers.isEmpty() && changedLayers.isEmpty()
               && beforeLayerOrder.isEmpty();
    }

    template <typename T>
    static void applyChange(
        T &target, const std::optional<ValueChange<T>> &change, bool forward)
    {
        if (change)
        {
            target = forward ? change->after : change->before;
        }
    }

    static bool reorderStrokes(
        QVector<Stroke> &strokes, const QVector<QUuid> &order)
    {
        if (order.isEmpty())
        {
            return true;
        }
        if (strokes.size() != order.size())
        {
            return false;
        }
        QHash<QUuid, int> indexes;
        indexes.reserve(strokes.size());
        for (int index = 0; index < strokes.size(); ++index)
        {
            if (indexes.contains(strokes[index].id))
            {
                return false;
            }
            indexes.insert(strokes[index].id, index);
        }
        QVector<Stroke> reordered;
        reordered.reserve(strokes.size());
        for (const QUuid &id : order)
        {
            const auto found = indexes.constFind(id);
            if (found == indexes.cend())
            {
                return false;
            }
            reordered.append(strokes[*found]);
        }
        strokes = std::move(reordered);
        return true;
    }

    static bool reorderLayers(
        QVector<Layer> &layers, const QVector<QUuid> &order)
    {
        if (order.isEmpty())
        {
            return true;
        }
        if (layers.size() != order.size())
        {
            return false;
        }
        QHash<QUuid, int> indexes;
        indexes.reserve(layers.size());
        for (int index = 0; index < layers.size(); ++index)
        {
            if (indexes.contains(layers[index].id))
            {
                return false;
            }
            indexes.insert(layers[index].id, index);
        }
        QVector<Layer> reordered;
        reordered.reserve(layers.size());
        for (const QUuid &id : order)
        {
            const auto found = indexes.constFind(id);
            if (found == indexes.cend())
            {
                return false;
            }
            reordered.append(layers[*found]);
        }
        layers = std::move(reordered);
        return true;
    }

    static bool applyStrokeDelta(QVector<Stroke> &strokes,
        const StrokeSequenceDelta &delta,
        bool forward)
    {
        if (delta.isEmpty())
        {
            return true;
        }
        QHash<QUuid, int> currentIndexes;
        currentIndexes.reserve(strokes.size());
        for (int index = 0; index < strokes.size(); ++index)
        {
            if (currentIndexes.contains(strokes[index].id))
            {
                return false;
            }
            currentIndexes.insert(strokes[index].id, index);
        }
        for (const ReplacedStroke &replacement : delta.replaced)
        {
            const auto found = currentIndexes.constFind(replacement.id);
            if (found == currentIndexes.cend())
            {
                return false;
            }
            strokes[*found] = forward ? replacement.after : replacement.before;
        }

        const QVector<IndexedStroke> &removals =
            forward ? delta.removed : delta.added;
        QSet<QUuid> removalIds;
        removalIds.reserve(removals.size());
        for (const IndexedStroke &entry : removals)
        {
            if (entry.index < 0 || entry.index >= strokes.size()
                || strokes[entry.index].id != entry.stroke.id
                || removalIds.contains(entry.stroke.id))
            {
                return false;
            }
            removalIds.insert(entry.stroke.id);
        }
        QVector<Stroke> retained;
        retained.reserve(strokes.size() - removals.size());
        for (const Stroke &stroke : std::as_const(strokes))
        {
            if (!removalIds.contains(stroke.id))
            {
                retained.append(stroke);
            }
        }
        if (retained.size() != strokes.size() - removals.size())
        {
            return false;
        }

        const QVector<IndexedStroke> &additions =
            forward ? delta.added : delta.removed;
        const int targetSize = retained.size() + additions.size();
        QSet<QUuid> targetIds;
        targetIds.reserve(targetSize);
        for (const Stroke &stroke : std::as_const(retained))
        {
            targetIds.insert(stroke.id);
        }
        int previousAdditionIndex = -1;
        for (const IndexedStroke &entry : additions)
        {
            if (entry.index <= previousAdditionIndex || entry.index < 0
                || entry.index >= targetSize
                || targetIds.contains(entry.stroke.id))
            {
                return false;
            }
            previousAdditionIndex = entry.index;
            targetIds.insert(entry.stroke.id);
        }

        QVector<Stroke> rebuilt;
        rebuilt.reserve(targetSize);
        int retainedIndex = 0;
        int additionIndex = 0;
        for (int targetIndex = 0; targetIndex < targetSize; ++targetIndex)
        {
            if (additionIndex < additions.size()
                && additions[additionIndex].index == targetIndex)
            {
                rebuilt.append(additions[additionIndex].stroke);
                ++additionIndex;
            }
            else
            {
                if (retainedIndex >= retained.size())
                {
                    return false;
                }
                rebuilt.append(retained[retainedIndex]);
                ++retainedIndex;
            }
        }
        if (retainedIndex != retained.size()
            || additionIndex != additions.size())
        {
            return false;
        }
        strokes = std::move(rebuilt);
        return reorderStrokes(
            strokes, forward ? delta.afterOrder : delta.beforeOrder);
    }

    bool apply(Document &document, bool forward) const
    {
        applyChange(document.size, size, forward);
        applyChange(document.background, background, forward);
        applyChange(document.animationFrames, animationFrames, forward);
        applyChange(document.framesPerSecond, framesPerSecond, forward);
        applyChange(document.wobbleAmount, wobbleAmount, forward);

        for (const LayerChange &change : changedLayers)
        {
            Layer *layer = document.layer(change.id);
            if (!layer)
            {
                return false;
            }
            applyChange(layer->name, change.name, forward);
            applyChange(layer->visible, change.visible, forward);
            applyChange(layer->reference, change.reference, forward);
            applyChange(layer->opacity, change.opacity, forward);
            applyChange(layer->blendMode, change.blendMode, forward);
            applyChange(layer->parentGroupId, change.parentGroupId, forward);
            applyChange(
                layer->clipToLayerBelow, change.clipToLayerBelow, forward);
            applyChange(
                layer->initialCanvasSize, change.initialCanvasSize, forward);
            if (!applyStrokeDelta(layer->strokes, change.strokes, forward))
            {
                return false;
            }
        }

        const QVector<IndexedLayer> &removals =
            forward ? removedLayers : addedLayers;
        for (auto entry = removals.crbegin(); entry != removals.crend();
            ++entry)
        {
            const int index = document.layerIndex(entry->layer.id);
            if (index < 0)
            {
                return false;
            }
            document.layers.removeAt(index);
        }

        const QVector<IndexedLayer> &additions =
            forward ? addedLayers : removedLayers;
        for (const IndexedLayer &entry : additions)
        {
            if (entry.index < 0 || entry.index > document.layers.size()
                || document.layer(entry.layer.id))
            {
                return false;
            }
            document.layers.insert(entry.index, entry.layer);
        }
        if (!reorderLayers(
                document.layers, forward ? afterLayerOrder : beforeLayerOrder))
        {
            return false;
        }
        applyChange(document.activeLayerId, activeLayerId, forward);
        return true;
    }

    bool mergeScalar(const DocumentDelta &next, int mergeId, const QUuid &scope)
    {
        const auto hasNoStructure = [](const DocumentDelta &delta)
        {
            return !delta.size && !delta.background && !delta.activeLayerId
                   && delta.removedLayers.isEmpty()
                   && delta.addedLayers.isEmpty()
                   && delta.beforeLayerOrder.isEmpty();
        };
        if (!hasNoStructure(*this) || !hasNoStructure(next))
        {
            return false;
        }

        const auto merge = []<typename T>(
                               std::optional<ValueChange<T>> &current,
                               const std::optional<ValueChange<T>> &later)
        {
            if (!current || !later || current->after != later->before)
            {
                return false;
            }
            current->after = later->after;
            return true;
        };
        const bool noLayerChanges =
            changedLayers.isEmpty() && next.changedLayers.isEmpty();
        if (mergeId == wobbleAmountMergeId && noLayerChanges && !animationFrames
            && !next.animationFrames && !framesPerSecond
            && !next.framesPerSecond && !size && !next.size)
        {
            return merge(wobbleAmount, next.wobbleAmount);
        }
        if (mergeId == animationFramesMergeId && noLayerChanges && !wobbleAmount
            && !next.wobbleAmount && !framesPerSecond && !next.framesPerSecond)
        {
            return merge(animationFrames, next.animationFrames);
        }
        if (mergeId == framesPerSecondMergeId && noLayerChanges && !wobbleAmount
            && !next.wobbleAmount && !animationFrames && !next.animationFrames)
        {
            return merge(framesPerSecond, next.framesPerSecond);
        }
        if (mergeId != layerOpacityMergeId || scope.isNull() || wobbleAmount
            || next.wobbleAmount || animationFrames || next.animationFrames
            || framesPerSecond || next.framesPerSecond
            || changedLayers.size() != 1 || next.changedLayers.size() != 1)
        {
            return false;
        }
        LayerChange &current = changedLayers[0];
        const LayerChange &later = next.changedLayers[0];
        if (current.id != scope || later.id != scope || current.name
            || later.name || current.visible || later.visible
            || current.reference || later.reference || current.blendMode
            || later.blendMode || current.parentGroupId || later.parentGroupId
            || current.clipToLayerBelow || later.clipToLayerBelow
            || current.initialCanvasSize || later.initialCanvasSize
            || !current.strokes.isEmpty() || !later.strokes.isEmpty())
        {
            return false;
        }
        return merge(current.opacity, later.opacity);
    }

    void normalizeMergedChanges()
    {
        const auto normalize = []<typename T>(
                                   std::optional<ValueChange<T>> &change)
        {
            if (change && change->before == change->after)
            {
                change.reset();
            }
        };
        normalize(size);
        normalize(background);
        normalize(animationFrames);
        normalize(framesPerSecond);
        normalize(wobbleAmount);
        normalize(activeLayerId);
        for (LayerChange &change : changedLayers)
        {
            normalize(change.name);
            normalize(change.visible);
            normalize(change.reference);
            normalize(change.opacity);
            normalize(change.blendMode);
            normalize(change.parentGroupId);
            normalize(change.clipToLayerBelow);
            normalize(change.initialCanvasSize);
        }
        changedLayers.removeIf(
            [](const LayerChange &change)
            {
                return change.isEmpty();
            });
    }

    QVector<Stroke> payloadStrokes(bool targetAfter) const
    {
        QVector<Stroke> strokes;
        const QVector<IndexedLayer> &layers =
            targetAfter ? addedLayers : removedLayers;
        qsizetype count = 0;
        for (const IndexedLayer &entry : layers)
        {
            count += entry.layer.strokes.size();
        }
        for (const LayerChange &change : changedLayers)
        {
            count += targetAfter ? change.strokes.added.size()
                                       + change.strokes.replaced.size()
                                 : change.strokes.removed.size()
                                       + change.strokes.replaced.size();
        }
        strokes.reserve(count);
        for (const IndexedLayer &entry : layers)
        {
            strokes += entry.layer.strokes;
        }
        for (const LayerChange &change : changedLayers)
        {
            const QVector<IndexedStroke> &indexed =
                targetAfter ? change.strokes.added : change.strokes.removed;
            for (const IndexedStroke &entry : indexed)
            {
                strokes.append(entry.stroke);
            }
            for (const ReplacedStroke &entry : change.strokes.replaced)
            {
                strokes.append(targetAfter ? entry.after : entry.before);
            }
        }
        return strokes;
    }

    static void accountStroke(
        HistoryMemoryFootprint &footprint, const Stroke &stroke)
    {
        accountVectorStorage(
            footprint, stroke.points, QStringLiteral("stroke-points"));
        accountImageStorage(footprint, stroke.clipMask);
        accountImageStorage(footprint, stroke.fillMask);
        if (stroke.pixelSelectionOp)
        {
            accountByteStorage(footprint, stroke.pixelSelectionOp->packedMask);
        }
    }

    static void accountLayer(
        HistoryMemoryFootprint &footprint, const Layer &layer)
    {
        accountVectorStorage(
            footprint, layer.strokes, QStringLiteral("layer-strokes"));
        for (const Stroke &stroke : layer.strokes)
        {
            accountStroke(footprint, stroke);
        }
        footprint.addOwned(static_cast<qint64>(layer.name.capacity())
                           * static_cast<qint64>(sizeof(QChar)));
    }

    void accountStorage(HistoryMemoryFootprint &footprint) const
    {
        footprint.addOwned(sizeof(DocumentDelta));
        footprint.addOwned(static_cast<qint64>(removedLayers.capacity())
                               * static_cast<qint64>(sizeof(IndexedLayer))
                           + static_cast<qint64>(addedLayers.capacity())
                                 * static_cast<qint64>(sizeof(IndexedLayer))
                           + static_cast<qint64>(changedLayers.capacity())
                                 * static_cast<qint64>(sizeof(LayerChange))
                           + static_cast<qint64>(beforeLayerOrder.capacity())
                                 * static_cast<qint64>(sizeof(QUuid))
                           + static_cast<qint64>(afterLayerOrder.capacity())
                                 * static_cast<qint64>(sizeof(QUuid)));
        for (const IndexedLayer &entry : removedLayers)
        {
            accountLayer(footprint, entry.layer);
        }
        for (const IndexedLayer &entry : addedLayers)
        {
            accountLayer(footprint, entry.layer);
        }
        for (const LayerChange &change : changedLayers)
        {
            if (change.name)
            {
                footprint.addOwned(
                    static_cast<qint64>(change.name->before.capacity()
                                        + change.name->after.capacity())
                    * static_cast<qint64>(sizeof(QChar)));
            }
            const StrokeSequenceDelta &strokes = change.strokes;
            footprint.addOwned(
                static_cast<qint64>(strokes.removed.capacity())
                    * static_cast<qint64>(sizeof(IndexedStroke))
                + static_cast<qint64>(strokes.added.capacity())
                      * static_cast<qint64>(sizeof(IndexedStroke))
                + static_cast<qint64>(strokes.replaced.capacity())
                      * static_cast<qint64>(sizeof(ReplacedStroke))
                + static_cast<qint64>(strokes.beforeOrder.capacity()
                                      + strokes.afterOrder.capacity())
                      * static_cast<qint64>(sizeof(QUuid)));
            for (const IndexedStroke &entry : strokes.removed)
            {
                accountStroke(footprint, entry.stroke);
            }
            for (const IndexedStroke &entry : strokes.added)
            {
                accountStroke(footprint, entry.stroke);
            }
            for (const ReplacedStroke &entry : strokes.replaced)
            {
                accountStroke(footprint, entry.before);
                accountStroke(footprint, entry.after);
            }
        }
    }

    DocumentUndoStack::StorageStats storageStats() const
    {
        DocumentUndoStack::StorageStats stats;
        stats.retainedLayers = removedLayers.size() + addedLayers.size();
        for (const IndexedLayer &entry : removedLayers)
        {
            stats.retainedStrokes += entry.layer.strokes.size();
        }
        for (const IndexedLayer &entry : addedLayers)
        {
            stats.retainedStrokes += entry.layer.strokes.size();
        }
        for (const LayerChange &change : changedLayers)
        {
            stats.retainedStrokes += change.strokes.retainedStrokeCount();
        }
        return stats;
    }
};

class LogicalHistoryCommand : public QUndoCommand
{
public:
    using QUndoCommand::QUndoCommand;

    virtual bool preflight(bool forward) = 0;
    virtual void clearPreflight() = 0;
    virtual DocumentUndoStack::StorageStats storageStats() const = 0;
    virtual void accountStorage(HistoryMemoryFootprint &footprint) const = 0;
};

class DocumentController::DocumentCommand final : public LogicalHistoryCommand
{
public:
    DocumentCommand(DocumentController *owner,
        QString text,
        DocumentDelta delta,
        PreparedState initialRedo,
        ActiveLayerPolicy activeLayerPolicy,
        std::shared_ptr<const HistoryEffects> effects,
        DocumentSerializer::ImmutableBackingLease beforeLease,
        DocumentSerializer::ImmutableBackingLease afterLease,
        int mergeId,
        QUuid mergeScope,
        quint64 beforeNode,
        quint64 afterNode,
        quint64 beforeRevision,
        quint64 afterRevision,
        qint64 beforeCompactSize,
        qint64 afterCompactSize)
        : LogicalHistoryCommand(std::move(text))
        , m_owner(owner)
        , m_delta(std::move(delta))
        , m_initialRedo(std::move(initialRedo))
        , m_activeLayerPolicy(activeLayerPolicy)
        , m_effects(std::move(effects))
        , m_beforeLease(std::move(beforeLease))
        , m_afterLease(std::move(afterLease))
        , m_mergeId(mergeId)
        , m_mergeScope(std::move(mergeScope))
        , m_beforeNode(beforeNode)
        , m_afterNode(afterNode)
        , m_beforeRevision(beforeRevision)
        , m_afterRevision(afterRevision)
        , m_beforeCompactSize(beforeCompactSize)
        , m_afterCompactSize(afterCompactSize)
    {
    }

    int id() const override
    {
        return m_mergeId;
    }

    bool mergeWith(const QUndoCommand *other) override
    {
        const auto *command = dynamic_cast<const DocumentCommand *>(other);
        if (!command || command->m_owner != m_owner
            || command->m_mergeId != m_mergeId
            || command->m_mergeScope != m_mergeScope
            || command->m_activeLayerPolicy != m_activeLayerPolicy
            || m_afterNode != command->m_beforeNode || command->m_firstRedo
            || command->m_initialRedo || command->m_staged
            || !m_delta.mergeScalar(command->m_delta, m_mergeId, m_mergeScope))
        {
            return false;
        }
        m_afterNode = command->m_afterNode;
        m_afterRevision = command->m_afterRevision;
        m_afterCompactSize = command->m_afterCompactSize;
        m_afterLease = command->m_afterLease;
        m_effects = command->m_effects;
        m_delta.normalizeMergedChanges();
        if (m_delta.isEmpty())
        {
            setObsolete(true);
            m_owner->normalizeMergedNoOp(m_beforeNode, m_beforeRevision);
        }
        return true;
    }

    void redo() override
    {
        PreparedState target;
        ActiveLayerPolicy policy = ActiveLayerPolicy::UsePrepared;
        if (m_firstRedo)
        {
            m_firstRedo = false;
            target = std::exchange(m_initialRedo, {});
            policy = m_activeLayerPolicy;
        }
        else
        {
            target = std::exchange(m_staged, {});
        }
        requireReady(target, m_beforeNode, "redo");
        m_owner->applyPreparedState(target,
            policy,
            *m_effects,
            CommitDirection::Forward,
            m_afterNode,
            m_afterRevision);
    }

    void undo() override
    {
        PreparedState target = std::exchange(m_staged, {});
        requireReady(target, m_afterNode, "undo");
        m_owner->applyPreparedState(target,
            ActiveLayerPolicy::UsePrepared,
            *m_effects,
            CommitDirection::Reverse,
            m_beforeNode,
            m_beforeRevision);
    }

    bool preflight(bool forward) override
    {
        if (!m_owner || !m_owner->m_currentState || m_staged)
        {
            return false;
        }
        PreparedState cursor = m_owner->m_currentState;
        quint64 cursorNode = m_owner->m_currentHistoryNode;
        const quint64 sourceNode = forward ? m_beforeNode : m_afterNode;
        if (cursorNode != sourceNode)
        {
            return false;
        }
        Document candidate = cursor->document();
        if (!m_delta.apply(candidate, forward))
        {
            return false;
        }
        if (m_activeLayerPolicy == ActiveLayerPolicy::PreserveCurrentIfPresent)
        {
            const QUuid active = cursor->document().activeLayerId;
            const bool canPreserve = candidate.layers.isEmpty()
                                         ? active.isNull()
                                         : candidate.layer(active) != nullptr;
            if (canPreserve)
            {
                candidate.activeLayerId = active;
            }
        }

        PreparedState prepared = m_owner->prepareState(std::move(candidate),
            cursor.get(),
            forward ? &m_afterLease : &m_beforeLease,
            true);
        const qint64 expectedSize =
            forward ? m_afterCompactSize : m_beforeCompactSize;
        if (!prepared || prepared->compactSize() != expectedSize)
        {
            return false;
        }
        m_staged = prepared;
        return true;
    }

    void clearPreflight() override
    {
        m_staged.reset();
    }

    DocumentUndoStack::StorageStats storageStats() const override
    {
        DocumentUndoStack::StorageStats stats = m_delta.storageStats();
        stats.retainedPreparedDocuments =
            (m_initialRedo ? 1 : 0) + (m_staged ? 1 : 0);
        stats.stagedPreparedDocuments = m_staged ? 1 : 0;
        return stats;
    }

    void accountStorage(HistoryMemoryFootprint &footprint) const override
    {
        m_delta.accountStorage(footprint);
        if (m_effects)
        {
            m_effects->accountStorage(footprint);
        }
        footprint.addOwned(sizeof(DocumentCommand));
        footprint.addOwned(static_cast<qint64>(text().capacity())
                           * static_cast<qint64>(sizeof(QChar)));
    }

private:
    void requireReady(const PreparedState &target,
        quint64 expectedNode,
        const char *operation) const
    {
        if (!m_owner || !target || !target->isValid()
            || m_owner->m_currentHistoryNode != expectedNode)
        {
            qFatal("Document history invariant failed during %s", operation);
        }
    }

    DocumentController *m_owner = nullptr;
    DocumentDelta m_delta;
    PreparedState m_initialRedo;
    PreparedState m_staged;
    ActiveLayerPolicy m_activeLayerPolicy =
        ActiveLayerPolicy::PreserveCurrentIfPresent;
    std::shared_ptr<const HistoryEffects> m_effects;
    DocumentSerializer::ImmutableBackingLease m_beforeLease;
    DocumentSerializer::ImmutableBackingLease m_afterLease;
    int m_mergeId = -1;
    QUuid m_mergeScope;
    quint64 m_beforeNode = 0;
    quint64 m_afterNode = 0;
    quint64 m_beforeRevision = 0;
    quint64 m_afterRevision = 0;
    qint64 m_beforeCompactSize = 0;
    qint64 m_afterCompactSize = 0;
    bool m_firstRedo = true;
};

class DocumentController::TransientCommand final : public LogicalHistoryCommand
{
public:
    TransientCommand(DocumentController *owner,
        QString text,
        std::shared_ptr<const HistoryEffects> effects)
        : LogicalHistoryCommand(std::move(text))
        , m_owner(owner)
        , m_effects(std::move(effects))
    {
    }

    bool preflight(bool) override
    {
        return m_owner && m_effects;
    }

    void clearPreflight() override
    {
    }

    void redo() override
    {
        if (m_owner && m_effects)
        {
            m_owner->dispatchHistoryEffects(
                *m_effects, CommitDirection::Forward, false);
        }
    }

    void undo() override
    {
        if (m_owner && m_effects)
        {
            m_owner->dispatchHistoryEffects(
                *m_effects, CommitDirection::Reverse, false);
        }
    }

    DocumentUndoStack::StorageStats storageStats() const override
    {
        return {};
    }

    void accountStorage(HistoryMemoryFootprint &footprint) const override
    {
        if (m_effects)
        {
            m_effects->accountStorage(footprint);
        }
        footprint.addOwned(sizeof(TransientCommand));
        footprint.addOwned(static_cast<qint64>(text().capacity())
                           * static_cast<qint64>(sizeof(QChar)));
    }

private:
    DocumentController *m_owner = nullptr;
    std::shared_ptr<const HistoryEffects> m_effects;
};

struct DocumentUndoStack::Impl
{
    std::vector<std::unique_ptr<QUndoCommand>> entries;
    int index = 0;
    int cleanIndex = 0;
    int undoLimit = 64;
    qsizetype peakTransientPreparedDocuments = 0;
    std::vector<QPointer<QAction>> undoActions;
    std::vector<QPointer<QAction>> redoActions;
};

DocumentUndoStack::DocumentUndoStack(DocumentController *owner)
    : m_owner(owner)
    , m_impl(std::make_unique<Impl>())
{
}

DocumentUndoStack::~DocumentUndoStack() = default;

bool DocumentUndoStack::canUndo() const
{
    return m_impl && m_impl->index > 0;
}

bool DocumentUndoStack::canRedo() const
{
    return m_impl && m_impl->index < static_cast<int>(m_impl->entries.size());
}

bool DocumentUndoStack::isClean() const
{
    return m_impl && m_impl->cleanIndex >= 0
           && m_impl->index == m_impl->cleanIndex;
}

int DocumentUndoStack::count() const
{
    return m_impl ? static_cast<int>(m_impl->entries.size()) : 0;
}

int DocumentUndoStack::index() const
{
    return m_impl ? m_impl->index : 0;
}

int DocumentUndoStack::undoLimit() const
{
    return m_impl ? m_impl->undoLimit : 0;
}

void DocumentUndoStack::setUndoLimit(int limit)
{
    if (!m_impl || m_moving || hasOpenMacro() || limit < 0)
    {
        return;
    }
    m_impl->undoLimit = limit;
    enforceLimits();
    updateActions();
}

void DocumentUndoStack::setClean()
{
    if (!m_impl || m_moving || hasOpenMacro())
    {
        return;
    }
    m_impl->cleanIndex = m_impl->index;
}

void DocumentUndoStack::clear()
{
    if (!m_impl || m_moving)
    {
        return;
    }
    if (hasOpenMacro())
    {
        failOpenMacro();
        return;
    }
    m_impl->entries.clear();
    m_impl->index = 0;
    m_impl->cleanIndex = 0;
    updateActions();
}

void DocumentUndoStack::push(QUndoCommand *rawCommand)
{
    std::unique_ptr<QUndoCommand> command(rawCommand);
    if (!m_impl || !command || m_moving || hasOpenMacro())
    {
        return;
    }

    while (static_cast<int>(m_impl->entries.size()) > m_impl->index)
    {
        const int oldCount = static_cast<int>(m_impl->entries.size());
        if (m_impl->cleanIndex == oldCount)
        {
            m_impl->cleanIndex = -1;
        }
        m_impl->entries.pop_back();
    }

    const bool mayMergeAcrossCurrentBoundary =
        m_impl->cleanIndex != m_impl->index;
    QScopedValueRollback<bool> movement(m_moving, true);
    command->redo();

    bool merged = false;
    if (mayMergeAcrossCurrentBoundary && m_impl->index > 0
        && command->id() >= 0)
    {
        QUndoCommand *previous =
            m_impl->entries[static_cast<size_t>(m_impl->index - 1)].get();
        if (previous && previous->id() == command->id())
        {
            merged = previous->mergeWith(command.get());
            if (merged && previous->isObsolete())
            {
                m_impl->entries.erase(
                    m_impl->entries.begin() + (m_impl->index - 1));
                --m_impl->index;
            }
        }
    }
    if (!merged)
    {
        m_impl->entries.push_back(std::move(command));
        ++m_impl->index;
    }
    m_moving = false;
    enforceLimits();
    updateActions();
}

void DocumentUndoStack::undo()
{
    if (!m_impl || !m_owner || m_moving || hasOpenMacro() || !canUndo())
    {
        return;
    }
    QScopedValueRollback<bool> movement(m_moving, true);
    QUndoCommand *command =
        m_impl->entries[static_cast<size_t>(m_impl->index - 1)].get();
    if (!command || !m_owner->preflightHistoryMovement(command, false))
    {
        m_owner->clearHistoryPreflight(command);
        return;
    }
    m_impl->peakTransientPreparedDocuments =
        std::max(m_impl->peakTransientPreparedDocuments,
            m_owner->historyStorageStats(command).stagedPreparedDocuments);
    m_owner->applyHistoryMovement(command, false);
    --m_impl->index;
    m_owner->clearHistoryPreflight(command);
    m_moving = false;
    updateActions();
}

void DocumentUndoStack::redo()
{
    if (!m_impl || !m_owner || m_moving || hasOpenMacro() || !canRedo())
    {
        return;
    }
    QScopedValueRollback<bool> movement(m_moving, true);
    QUndoCommand *command =
        m_impl->entries[static_cast<size_t>(m_impl->index)].get();
    if (!command || !m_owner->preflightHistoryMovement(command, true))
    {
        m_owner->clearHistoryPreflight(command);
        return;
    }
    m_impl->peakTransientPreparedDocuments =
        std::max(m_impl->peakTransientPreparedDocuments,
            m_owner->historyStorageStats(command).stagedPreparedDocuments);
    m_owner->applyHistoryMovement(command, true);
    ++m_impl->index;
    m_owner->clearHistoryPreflight(command);
    m_moving = false;
    updateActions();
}

void DocumentUndoStack::beginMacro(const QString &text)
{
    if (!m_owner || m_moving)
    {
        return;
    }
    m_owner->beginHistoryMacro(text);
}

void DocumentUndoStack::endMacro()
{
    if (!m_owner || m_moving || !hasOpenMacro())
    {
        return;
    }
    m_owner->endHistoryMacro();
}

void DocumentUndoStack::failOpenMacro()
{
    if (m_owner)
    {
        m_owner->failHistoryMacro();
    }
}

bool DocumentUndoStack::hasOpenMacro() const
{
    return m_owner && m_owner->hasOpenHistoryMacro();
}

QAction *DocumentUndoStack::createUndoAction(QObject *parent)
{
    auto *action = new QAction(parent);
    QObject::connect(action,
        &QAction::triggered,
        this,
        [this]()
        {
            undo();
        });
    m_impl->undoActions.emplace_back(action);
    updateActions();
    return action;
}

QAction *DocumentUndoStack::createRedoAction(QObject *parent)
{
    auto *action = new QAction(parent);
    QObject::connect(action,
        &QAction::triggered,
        this,
        [this]()
        {
            redo();
        });
    m_impl->redoActions.emplace_back(action);
    updateActions();
    return action;
}

void DocumentUndoStack::updateActions()
{
    if (!m_impl)
    {
        return;
    }
    const QString undoText =
        canUndo() ? tr("Undo %1").arg(
                        m_impl->entries[static_cast<size_t>(m_impl->index - 1)]
                            ->text())
                  : tr("Undo");
    const QString redoText =
        canRedo()
            ? tr("Redo %1").arg(
                  m_impl->entries[static_cast<size_t>(m_impl->index)]->text())
            : tr("Redo");
    for (auto iterator = m_impl->undoActions.begin();
        iterator != m_impl->undoActions.end();)
    {
        if (!*iterator)
        {
            iterator = m_impl->undoActions.erase(iterator);
            continue;
        }
        (*iterator)->setEnabled(canUndo());
        (*iterator)->setText(undoText);
        ++iterator;
    }
    for (auto iterator = m_impl->redoActions.begin();
        iterator != m_impl->redoActions.end();)
    {
        if (!*iterator)
        {
            iterator = m_impl->redoActions.erase(iterator);
            continue;
        }
        (*iterator)->setEnabled(canRedo());
        (*iterator)->setText(redoText);
        ++iterator;
    }
}

DocumentUndoStack::StorageStats DocumentUndoStack::storageStats() const
{
    StorageStats total;
    if (!m_impl)
    {
        return total;
    }
    HistoryMemoryFootprint footprint;
    total.entryCount = static_cast<qsizetype>(m_impl->entries.size());
    total.peakTransientPreparedDocuments =
        m_impl->peakTransientPreparedDocuments;
    if (m_owner && m_owner->m_macroTransaction)
    {
        total.macroPreparedDocuments =
            (m_owner->m_macroTransaction->startState ? 1 : 0)
            + (m_owner->m_macroTransaction->workingState ? 1 : 0);
    }
    for (const std::unique_ptr<QUndoCommand> &entry : m_impl->entries)
    {
        const auto *logical =
            dynamic_cast<const LogicalHistoryCommand *>(entry.get());
        if (!logical)
        {
            continue;
        }
        const StorageStats command = logical->storageStats();
        total.retainedLayers += command.retainedLayers;
        total.retainedStrokes += command.retainedStrokes;
        total.retainedPreparedDocuments += command.retainedPreparedDocuments;
        total.stagedPreparedDocuments += command.stagedPreparedDocuments;
        logical->accountStorage(footprint);
    }
    total.retainedBytes = footprint.totalBytes();
    total.residentBudgetSoftExceeded =
        total.entryCount == 1 && total.retainedBytes > m_maximumResidentBytes;
    return total;
}

void DocumentUndoStack::enforceLimits()
{
    if (!m_impl)
    {
        return;
    }
    while (m_impl->entries.size() > 1)
    {
        const StorageStats stats = storageStats();
        const bool overCount =
            m_impl->undoLimit > 0
            && static_cast<int>(m_impl->entries.size()) > m_impl->undoLimit;
        const bool overBytes = stats.retainedBytes > m_maximumResidentBytes;
        if (!overCount && !overBytes)
        {
            break;
        }

        const int count = static_cast<int>(m_impl->entries.size());
        const int undoDistance = m_impl->index;
        const int redoDistance = count - m_impl->index;
        const bool removePrefix =
            undoDistance > 0
            && (redoDistance == 0 || undoDistance >= redoDistance);
        if (removePrefix)
        {
            m_impl->entries.erase(m_impl->entries.begin());
            --m_impl->index;
            if (m_impl->cleanIndex == 0)
            {
                m_impl->cleanIndex = -1;
            }
            else if (m_impl->cleanIndex > 0)
            {
                --m_impl->cleanIndex;
            }
        }
        else
        {
            const int oldCount = count;
            m_impl->entries.pop_back();
            if (m_impl->cleanIndex == oldCount)
            {
                m_impl->cleanIndex = -1;
            }
        }
    }
}

DocumentController::DocumentController(QObject *parent)
    : QObject(parent)
    , m_undoStack(this)
{
    m_currentState =
        prepareState(Document::createDefault(QSize(1024, 768), tr("Layer 1")));
    Q_ASSERT(m_currentState);
    m_undoStack.setUndoLimit(64);
    m_undoStack.setClean();
}

DocumentController::~DocumentController() = default;

const Document &DocumentController::document() const
{
    static const Document empty;
    const PreparedState &state = editableState();
    return state ? state->document() : empty;
}

DocumentUndoStack *DocumentController::undoStack()
{
    return &m_undoStack;
}

bool DocumentController::isModified() const
{
    return m_currentContentRevision != m_savedContentRevision;
}

void DocumentController::releaseTransientCaches()
{
    m_serializationCache.clear();
}

bool DocumentController::selectionHasVisibleLayerPixels(
    const QUuid &layerId, const QImage &selectionMask, int preferredFrame) const
{
    const Document &current = document();
    const Layer *layer = current.layer(layerId);
    if (!layer || selectionMask.isNull() || selectionMask.size() != current.size
        || selectionMask.format() != QImage::Format_Grayscale8)
    {
        return false;
    }
    const qint64 maskKey = selectionMask.cacheKey();
    if (m_selectionVisibilityCacheValid
        && m_selectionVisibilityLayerId == layerId
        && m_selectionVisibilityMaskKey == maskKey)
    {
        return m_selectionVisibilityCacheResult;
    }

    const SelectionVisibility::Result visibility =
        SelectionVisibility::evaluate(
            current, *layer, selectionMask, preferredFrame);
    m_selectionVisibilityCacheValid = visibility.renderSucceeded;
    m_selectionVisibilityLayerId = layerId;
    m_selectionVisibilityMaskKey = maskKey;
    m_selectionVisibilityCacheResult = visibility.hasVisiblePixels;
    return visibility.hasVisiblePixels;
}

void DocumentController::pushSelectionStateCommand(const QString &text,
    const QUuid &beforeLayerId,
    const QImage &beforeMask,
    const QUuid &afterLayerId,
    const QImage &afterMask)
{
    if (m_undoStack.m_moving)
    {
        return;
    }
    const auto snapshot =
        [](const QImage &mask) -> std::optional<PackedMaskRegion>
    {
        return mask.isNull() ? std::optional<PackedMaskRegion>()
                             : packBinaryMask(mask);
    };
    const std::optional<PackedMaskRegion> before = snapshot(beforeMask);
    const std::optional<PackedMaskRegion> after = snapshot(afterMask);
    if ((!beforeMask.isNull() && !before) || (!afterMask.isNull() && !after))
    {
        failHistoryMacro();
        return;
    }
    auto effects = std::make_shared<HistoryEffects>();
    effects->selectionState = HistoryEffects::SelectionStateTransition{
        {beforeLayerId, before}, {afterLayerId, after}};
    if (!effects->hasSelectionTransition())
    {
        return;
    }
    auto frozenEffects =
        std::make_shared<const HistoryEffects>(effects->frozenCopy());
    if (m_macroTransaction)
    {
        if (!m_macroTransaction->failed)
        {
            m_macroTransaction->effects.append(*frozenEffects);
        }
        return;
    }
    m_undoStack.push(
        new TransientCommand(this, text, std::move(frozenEffects)));
}

bool DocumentController::newDocument(const QSize &size, QString *error)
{
    const QSize normalized(std::clamp(size.width(),
                               DocumentLimits::minimumCanvasEdge,
                               DocumentLimits::maximumCanvasEdge),
        std::clamp(size.height(),
            DocumentLimits::minimumCanvasEdge,
            DocumentLimits::maximumCanvasEdge));
    Document document;
    try
    {
        document = Document::createDefault(normalized, tr("Layer 1"));
    }
    catch (const std::bad_alloc &)
    {
        if (error)
        {
            *error = tr("There is not enough memory to prepare the document.");
        }
        return false;
    }
    return replaceDocument(
        std::move(document), DocumentReplacementDisposition::Clean, error);
}

bool DocumentController::loadDocument(Document document, QString *error)
{
    return replaceDocument(
        std::move(document), DocumentReplacementDisposition::Clean, error);
}

bool DocumentController::loadRecoveredDocument(
    Document document, QString *error)
{
    return replaceDocument(
        std::move(document), DocumentReplacementDisposition::Recovered, error);
}

bool DocumentController::replaceDocument(Document document,
    DocumentReplacementDisposition disposition,
    QString *error)
{
    if (error)
    {
        error->clear();
    }
    if (m_undoStack.m_moving || hasOpenHistoryMacro())
    {
        if (error)
        {
            *error =
                tr("Cannot replace a document during a history transaction.");
        }
        return false;
    }
    if (m_documentReplacementInProgress)
    {
        if (error)
        {
            *error = tr("Cannot replace a document during another document "
                        "transition.");
        }
        return false;
    }

    QScopedValueRollback<bool> replacement(
        m_documentReplacementInProgress, true);
    PreparedState prepared;
    try
    {
        ensureActiveLayer(document);
        if (m_failNextDocumentReplacementPreparationForTesting)
        {
            m_failNextDocumentReplacementPreparationForTesting = false;
            if (error)
            {
                *error = tr("The document could not be prepared.");
            }
            return false;
        }
        prepared =
            prepareState(std::move(document), nullptr, nullptr, false, error);
    }
    catch (const std::bad_alloc &)
    {
        if (error)
        {
            *error = tr("There is not enough memory to prepare the document.");
        }
        return false;
    }
    if (!prepared)
    {
        if (error && error->isEmpty())
        {
            *error = tr("The document could not be prepared.");
        }
        return false;
    }

    const bool wasModified = isModified();
    m_currentState = prepared;
    const bool recovered =
        disposition == DocumentReplacementDisposition::Recovered;
    m_currentContentRevision = recovered ? 1 : 0;
    m_savedContentRevision = 0;
    m_nextContentRevision = recovered ? 1 : 0;
    m_currentHistoryNode = 0;
    m_nextHistoryNode = 0;
    m_selectionVisibilityCacheValid = false;
    m_undoStack.clear();
    m_undoStack.setClean();
    emit documentReplaced();
    emit documentChanged();
    emit layerThumbnailsReset();
    emit activeLayerChanged(this->document().activeLayerId);
    const bool modified = isModified();
    if (modified != wasModified)
    {
        emit modifiedChanged(modified);
    }
    return true;
}

bool DocumentController::saveDocument(const QString &filePath, QString *error)
{
    if (m_undoStack.m_moving || hasOpenHistoryMacro())
    {
        failHistoryMacro();
        if (error)
        {
            *error = tr("Cannot save an unfinished history transaction.");
        }
        return rejectHistoryMutation();
    }
    return m_currentState
           && DocumentSerializer::save(
               filePath, *m_currentState, m_serializationCache, error);
}

QByteArray DocumentController::serializeDocument(
    const QJsonObject &additionalRootFields, QString *error)
{
    if (error)
    {
        error->clear();
    }
    if (m_undoStack.m_moving || hasOpenHistoryMacro())
    {
        if (error)
        {
            *error = tr("Cannot save an unfinished history transaction.");
        }
        return {};
    }
    return m_currentState ? DocumentSerializer::toJson(*m_currentState,
                                m_serializationCache,
                                additionalRootFields)
                          : QByteArray();
}

void DocumentController::markSaved()
{
    if (m_undoStack.m_moving || hasOpenHistoryMacro())
    {
        failHistoryMacro();
        return;
    }
    const bool wasModified = isModified();
    m_savedContentRevision = m_currentContentRevision;
    m_undoStack.setClean();
    if (wasModified)
    {
        emit modifiedChanged(false);
    }
}

bool DocumentController::resizeImage(const QSize &size)
{
    const Document &current = document();
    if (size == current.size || size.width() < DocumentLimits::minimumCanvasEdge
        || size.height() < DocumentLimits::minimumCanvasEdge
        || size.width() > DocumentLimits::maximumCanvasEdge
        || size.height() > DocumentLimits::maximumCanvasEdge)
    {
        return rejectHistoryMutation();
    }

    Document resized = current;
    const qreal horizontalScale =
        static_cast<qreal>(size.width()) / current.size.width();
    const qreal verticalScale =
        static_cast<qreal>(size.height()) / current.size.height();
    QTransform transform;
    transform.scale(horizontalScale, verticalScale);
    resized.size = size;
    for (Layer &layer : resized.layers)
    {
        if (!layerCanProducePixels(layer))
        {
            layer.strokes.clear();
            layer.initialCanvasSize = size;
            continue;
        }
        if (!layer.initialCanvasSize.isValid())
        {
            layer.initialCanvasSize = current.size;
        }
        if (layer.strokes.size() >= DocumentLimits::maximumStrokesPerLayer)
        {
            return rejectHistoryMutation();
        }
        Stroke reframe;
        reframe.mode = StrokeMode::Reframe;
        reframe.reframeOp = ReframeOp{ReframeMode::Image,
            SamplingMode::Smooth,
            current.size,
            size,
            QPoint()};
        reframe.points.clear();
        layer.strokes.append(std::move(reframe));
    }
    if (totalStrokeCount(resized) > DocumentLimits::maximumTotalStrokes
        || distinctClipMaskBytes(resized)
               > DocumentLimits::maximumDistinctClipMaskBytes)
    {
        return rejectHistoryMutation();
    }

    bool invertible = false;
    const QTransform inverse = transform.inverted(&invertible);
    if (!invertible)
    {
        return rejectHistoryMutation();
    }
    auto effects = std::make_shared<HistoryEffects>();
    const QSize previousSize = current.size;
    effects->beforeDocumentChanged.append(
        HistoryEffects::CanvasResize{previousSize, size, transform, inverse});
    effects->afterDocumentChanged.append(
        HistoryEffects::LayerThumbnailsReset{});
    return tryCommitCandidate(
        tr("Resize image"), std::move(resized), std::move(effects));
}

bool DocumentController::resizeCanvas(
    const QSize &size, const QPoint &contentOffset)
{
    const Document &current = document();
    if ((size == current.size && contentOffset.isNull())
        || size.width() < DocumentLimits::minimumCanvasEdge
        || size.height() < DocumentLimits::minimumCanvasEdge
        || size.width() > DocumentLimits::maximumCanvasEdge
        || size.height() > DocumentLimits::maximumCanvasEdge
        || std::abs(static_cast<qreal>(contentOffset.x()))
               > DocumentLimits::maximumStoredCoordinateMagnitude
        || std::abs(static_cast<qreal>(contentOffset.y()))
               > DocumentLimits::maximumStoredCoordinateMagnitude)
    {
        return rejectHistoryMutation();
    }

    QTransform transform;
    transform.translate(contentOffset.x(), contentOffset.y());
    bool invertible = false;
    const QTransform inverse = transform.inverted(&invertible);
    if (!invertible)
    {
        return rejectHistoryMutation();
    }

    Document resized = current;
    resized.size = size;

    for (Layer &layer : resized.layers)
    {
        if (!layerCanProducePixels(layer))
        {
            layer.strokes.clear();
            layer.initialCanvasSize = size;
            continue;
        }
        if (!layer.initialCanvasSize.isValid())
        {
            layer.initialCanvasSize = current.size;
        }
        if (layer.strokes.size() >= DocumentLimits::maximumStrokesPerLayer)
        {
            return rejectHistoryMutation();
        }
        Stroke reframe;
        reframe.mode = StrokeMode::Reframe;
        reframe.reframeOp = ReframeOp{ReframeMode::Canvas,
            SamplingMode::Nearest,
            current.size,
            size,
            contentOffset};
        reframe.points.clear();
        layer.strokes.append(std::move(reframe));
    }
    if (totalStrokeCount(resized) > DocumentLimits::maximumTotalStrokes
        || distinctClipMaskBytes(resized)
               > DocumentLimits::maximumDistinctClipMaskBytes)
    {
        return rejectHistoryMutation();
    }

    auto effects = std::make_shared<HistoryEffects>();
    const QSize previousSize = current.size;
    effects->beforeDocumentChanged.append(
        HistoryEffects::CanvasResize{previousSize, size, transform, inverse});
    effects->afterDocumentChanged.append(
        HistoryEffects::LayerThumbnailsReset{});
    return tryCommitCandidate(
        tr("Resize canvas"), std::move(resized), std::move(effects));
}

void DocumentController::setActiveLayer(const QUuid &id)
{
    if (hasOpenHistoryMacro())
    {
        failHistoryMacro();
        return;
    }
    const Document &current = document();
    const Layer *layer = current.layer(id);
    if (current.activeLayerId == id || !layer
        || layer->kind != LayerKind::Paint)
    {
        return;
    }
    std::optional<PreparedDocument> rebound =
        DocumentSerializer::rebindActiveLayer(*m_currentState, id);
    if (!rebound)
    {
        return;
    }
    m_currentState =
        std::make_shared<const PreparedDocument>(std::move(*rebound));
    emit activeLayerChanged(id);
}

DocumentController::AddStrokeResult DocumentController::addStroke(
    const QUuid &layerId, Stroke stroke)
{
    const auto reject = [this](AddStrokeResult result)
    {
        failHistoryMacro();
        return result;
    };
    const PreparedState before = editableState();
    if (!before)
    {
        return reject(AddStrokeResult::RejectedCommit);
    }
    const Document &current = document();
    const Layer *layer = current.layer(layerId);
    if (!layer || layer->kind != LayerKind::Paint)
    {
        return reject(AddStrokeResult::RejectedInvalidLayer);
    }
    if (layer->strokes.size() >= DocumentLimits::maximumStrokesPerLayer
        || before->totalStrokeCount() >= DocumentLimits::maximumTotalStrokes)
    {
        return reject(AddStrokeResult::RejectedStrokeLimit);
    }
    if (stroke.id.isNull()
        || (stroke.mode != StrokeMode::Paint && stroke.mode != StrokeMode::Erase
            && stroke.mode != StrokeMode::Fill)
        || !stroke.color.isValid() || !std::isfinite(stroke.width)
        || stroke.width < DocumentLimits::minimumStrokeWidth
        || stroke.width > DocumentLimits::maximumStrokeWidth
        || !isValidBrushSettings(stroke.brush) || stroke.points.isEmpty()
        || (stroke.visibilityClip
            && (stroke.visibilityClip->isEmpty()
                || !QRect(QPoint(), current.size)
                    .contains(*stroke.visibilityClip)))
        || (!stroke.clipMask.isNull()
            && (stroke.clipMask.size() != current.size
                || stroke.clipMask.format() != QImage::Format_Grayscale8))
        || (!stroke.fillMask.isNull()
            && (stroke.mode != StrokeMode::Fill
                || stroke.fillMask.size() != current.size
                || stroke.fillMask.format() != QImage::Format_Grayscale8)))
    {
        return reject(AddStrokeResult::RejectedInvalidStroke);
    }
    if (!std::all_of(stroke.points.cbegin(),
            stroke.points.cend(),
            [&current](const StrokePoint &point)
            {
                return isValidInputStrokePoint(point, current.size);
            }))
    {
        return reject(AddStrokeResult::RejectedInvalidStroke);
    }

    const qsizetype currentPointCount = before->totalPointCount();
    if (currentPointCount >= DocumentLimits::maximumTotalPoints)
    {
        return reject(AddStrokeResult::RejectedPointLimit);
    }
    const qsizetype availablePoints =
        DocumentLimits::maximumTotalPoints - currentPointCount;
    const qsizetype acceptedPointCount = std::min(stroke.points.size(),
        std::min(static_cast<qsizetype>(DocumentLimits::maximumPointsPerStroke),
            availablePoints));
    const bool pointsResampled = acceptedPointCount < stroke.points.size();
    if (pointsResampled && acceptedPointCount < 2)
    {
        return reject(AddStrokeResult::RejectedPointLimit);
    }
    if (pointsResampled)
    {
        stroke.points = resampleStrokePoints(stroke.points, acceptedPointCount);
    }
    if (!canonicalizeStrokeVisibility(stroke, current.size))
    {
        return reject(AddStrokeResult::RejectedInvalidStroke);
    }

    const QUuid strokeId = stroke.id;
    const std::optional<PackedMaskRegion> clipMask =
        stroke.clipMask.isNull() ? std::optional<PackedMaskRegion>()
                                 : packBinaryMask(stroke.clipMask);
    if (!stroke.clipMask.isNull() && !clipMask)
    {
        return reject(AddStrokeResult::RejectedMaskLimit);
    }
    auto effects = std::make_shared<HistoryEffects>();
    effects->beforeDocumentChanged.append(
        HistoryEffects::StrokePresence{layerId, strokeId, clipMask});
    effects->afterDocumentChanged.append(
        HistoryEffects::LayerThumbnail{layerId});

    DocumentSerializer::AppendStrokeResult appended =
        DocumentSerializer::appendStroke(*before,
            layerId,
            stroke,
            m_serializationCache,
            DocumentLimits::maximumProjectBytes);
    switch (appended.status)
    {
    case DocumentSerializer::AppendStrokeStatus::Appended:
    {
        PreparedState after = std::make_shared<const PreparedDocument>(
            std::move(appended.prepared));
        if (!tryCommitPreparedCandidate(tr("Draw stroke"),
                before,
                std::move(after),
                std::move(effects),
                ActiveLayerPolicy::PreserveCurrentIfPresent,
                -1,
                {},
                layerId))
        {
            return AddStrokeResult::RejectedCommit;
        }
        return pointsResampled ? AddStrokeResult::AddedWithResampledPoints
                               : AddStrokeResult::Added;
    }
    case DocumentSerializer::AppendStrokeStatus::NotApplicable:
    {
        Document candidate = current;
        candidate.layer(layerId)->strokes.append(stroke);
        if (distinctClipMaskBytes(candidate)
            > DocumentLimits::maximumDistinctClipMaskBytes)
        {
            return reject(AddStrokeResult::RejectedMaskLimit);
        }
        if (!tryCommitCandidate(
                tr("Draw stroke"), std::move(candidate), std::move(effects)))
        {
            return AddStrokeResult::RejectedCommit;
        }
        return pointsResampled ? AddStrokeResult::AddedWithResampledPoints
                               : AddStrokeResult::Added;
    }
    case DocumentSerializer::AppendStrokeStatus::StrokeLimit:
        return reject(AddStrokeResult::RejectedStrokeLimit);
    case DocumentSerializer::AppendStrokeStatus::PointLimit:
        return reject(AddStrokeResult::RejectedPointLimit);
    case DocumentSerializer::AppendStrokeStatus::MaskLimit:
        return reject(AddStrokeResult::RejectedMaskLimit);
    case DocumentSerializer::AppendStrokeStatus::Invalid:
        return reject(AddStrokeResult::RejectedInvalidStroke);
    case DocumentSerializer::AppendStrokeStatus::TooLarge:
        return reject(AddStrokeResult::RejectedCommit);
    }
    return reject(AddStrokeResult::RejectedCommit);
}

bool DocumentController::moveStrokes(const QUuid &layerId,
    const QVector<QUuid> &strokeIds,
    const QPointF &delta,
    const QImage &selectionMask)
{
    if (!std::isfinite(delta.x()) || !std::isfinite(delta.y())
        || (qFuzzyIsNull(delta.x()) && qFuzzyIsNull(delta.y())))
    {
        return rejectHistoryMutation();
    }
    QTransform transform;
    transform.translate(delta.x(), delta.y());
    return transformStrokes(layerId,
        strokeIds,
        transform,
        1.0,
        tr("Move selection"),
        selectionMask);
}

bool DocumentController::scaleStrokes(const QUuid &layerId,
    const QVector<QUuid> &strokeIds,
    const QPointF &center,
    qreal factor,
    const QImage &selectionMask)
{
    if (!std::isfinite(center.x()) || !std::isfinite(center.y())
        || !std::isfinite(factor) || factor <= 0.0)
    {
        return rejectHistoryMutation();
    }
    QTransform transform;
    transform.translate(center.x(), center.y());
    transform.scale(factor, factor);
    transform.translate(-center.x(), -center.y());
    return transformStrokes(layerId,
        strokeIds,
        transform,
        factor,
        tr("Scale selection"),
        selectionMask);
}

bool DocumentController::rotateStrokes(const QUuid &layerId,
    const QVector<QUuid> &strokeIds,
    const QPointF &center,
    qreal degrees,
    const QImage &selectionMask)
{
    if (!std::isfinite(center.x()) || !std::isfinite(center.y())
        || !std::isfinite(degrees) || qFuzzyIsNull(degrees))
    {
        return rejectHistoryMutation();
    }
    QTransform transform;
    transform.translate(center.x(), center.y());
    transform.rotate(degrees);
    transform.translate(-center.x(), -center.y());
    return transformStrokes(layerId,
        strokeIds,
        transform,
        1.0,
        tr("Rotate selection"),
        selectionMask);
}

bool DocumentController::flipStrokes(const QUuid &layerId,
    const QVector<QUuid> &strokeIds,
    const QPointF &center,
    bool horizontal,
    const QImage &selectionMask)
{
    if (!std::isfinite(center.x()) || !std::isfinite(center.y()))
    {
        return rejectHistoryMutation();
    }
    QTransform transform;
    transform.translate(center.x(), center.y());
    transform.scale(horizontal ? -1.0 : 1.0, horizontal ? 1.0 : -1.0);
    transform.translate(-center.x(), -center.y());
    return transformStrokes(layerId,
        strokeIds,
        transform,
        1.0,
        horizontal ? tr("Flip selection horizontally")
                   : tr("Flip selection vertically"),
        selectionMask);
}

bool DocumentController::transformSelection(const QUuid &layerId,
    const QVector<QUuid> &strokeIds,
    const QTransform &transform,
    const QImage &selectionMask)
{
    const bool finite =
        std::isfinite(transform.m11()) && std::isfinite(transform.m12())
        && std::isfinite(transform.m13()) && std::isfinite(transform.m21())
        && std::isfinite(transform.m22()) && std::isfinite(transform.m23())
        && std::isfinite(transform.m31()) && std::isfinite(transform.m32())
        && std::isfinite(transform.m33());
    const qreal determinant = transform.determinant();
    const qreal widthScale = std::sqrt(std::abs(determinant));
    if (!finite || !transform.isAffine() || transform.isIdentity()
        || !std::isfinite(widthScale) || widthScale <= 0.0)
    {
        return rejectHistoryMutation();
    }
    return transformStrokes(layerId,
        strokeIds,
        transform,
        widthScale,
        tr("Transform selection"),
        selectionMask);
}

bool DocumentController::duplicateStrokes(const QUuid &layerId,
    const QVector<QUuid> &strokeIds,
    const QPointF &delta,
    const QImage &selectionMask)
{
    const Document &current = document();
    if (strokeIds.isEmpty() || !std::isfinite(delta.x())
        || !std::isfinite(delta.y())
        || (!selectionMask.isNull()
            && (selectionMask.size() != current.size
                || selectionMask.format() != QImage::Format_Grayscale8)))
    {
        return rejectHistoryMutation();
    }

    const Layer *layer = current.layer(layerId);
    if (!layer)
    {
        return rejectHistoryMutation();
    }
    if (selectionMask.isNull()
        && std::any_of(layer->strokes.cbegin(),
            layer->strokes.cend(),
            [](const Stroke &stroke)
            {
                return stroke.mode == StrokeMode::PixelSelection
                       || stroke.mode == StrokeMode::Reframe;
            }))
    {
        return rejectHistoryMutation();
    }
    const QSet<QUuid> requested(strokeIds.cbegin(), strokeIds.cend());
    if (!selectionMask.isNull())
    {
        if (layer->strokes.size() >= DocumentLimits::maximumStrokesPerLayer
            || totalStrokeCount(current) >= DocumentLimits::maximumTotalStrokes
            || !std::any_of(layer->strokes.cbegin(),
                layer->strokes.cend(),
                [&requested](const Stroke &stroke)
                {
                    return requested.contains(stroke.id);
                })
            || !selectionHasVisibleLayerPixels(layerId, selectionMask))
        {
            return rejectHistoryMutation();
        }
        QTransform transform;
        transform.translate(delta.x(), delta.y());
        const std::optional<Stroke> operation =
            selectionOperationStroke(selectionMask, transform, false, true);
        if (!operation)
        {
            return rejectHistoryMutation();
        }
        const QVector<Stroke> before = layer->strokes;
        QVector<Stroke> after = before;
        after.append(*operation);
        Document withCopy = current;
        withCopy.layer(layerId)->strokes = after;
        if (distinctClipMaskBytes(withCopy)
            > DocumentLimits::maximumDistinctClipMaskBytes)
        {
            return rejectHistoryMutation();
        }
        const QImage nextSelectionMask =
            transformedSelectionSupport(selectionMask,
                current.size,
                transform,
                operation->pixelSelectionOp->sampling);
        if (!maskHasContent(nextSelectionMask))
        {
            return rejectHistoryMutation();
        }
        const PackedMaskRegion sourceMaskSnapshot{
            operation->pixelSelectionOp->canvasSize,
            operation->pixelSelectionOp->sourceBounds,
            operation->pixelSelectionOp->packedMask};
        const std::optional<PackedMaskRegion> nextMaskSnapshot =
            packBinaryMask(nextSelectionMask);
        if (!nextMaskSnapshot)
        {
            return rejectHistoryMutation();
        }
        const QVector<QUuid> sourceIds = strokeIds;
        const QVector<QUuid> resultIds{operation->id};
        auto effects = std::make_shared<HistoryEffects>();
        effects->beforeDocumentChanged.append(
            HistoryEffects::SelectionOverlay{layerId,
                sourceIds,
                resultIds,
                sourceMaskSnapshot,
                *nextMaskSnapshot});
        effects->afterDocumentChanged.append(
            HistoryEffects::LayerThumbnail{layerId});
        return tryCommitCandidate(
            tr("Duplicate selection"), std::move(withCopy), std::move(effects));
    }
    QVector<Stroke> copies;
    QVector<QUuid> sourceIds;
    QVector<QUuid> duplicateIds;
    qsizetype addedPoints = 0;
    QTransform transform;
    transform.translate(delta.x(), delta.y());
    QHash<QString, QImage> selectedMasks;
    QHash<qint64, QImage> transformedMasks;
    for (const Stroke &stroke : layer->strokes)
    {
        if (!requested.contains(stroke.id))
        {
            continue;
        }
        const QString sourceMaskKey = visibilityCacheKey(stroke);
        QImage duplicateMask;
        if (!selectionMask.isNull())
        {
            auto selected = selectedMasks.constFind(sourceMaskKey);
            if (selected == selectedMasks.cend())
            {
                const std::optional<QImage> visibility =
                    materializedVisibilityMask(stroke, current.size);
                if (!visibility)
                {
                    return rejectHistoryMutation();
                }
                selected = selectedMasks.insert(sourceMaskKey,
                    maskedPart(*visibility, selectionMask, true));
            }
            if (!maskHasContent(selected.value())
                || (stroke.mode == StrokeMode::Fill
                    && !masksIntersect(stroke.fillMask, selected.value())))
            {
                continue;
            }
            duplicateMask =
                transformedMask(selected.value(), current.size, transform);
            if (!maskHasContent(duplicateMask))
            {
                continue;
            }
        }

        Stroke copy = stroke;
        copy.id = QUuid::createUuid();
        for (StrokePoint &point : copy.points)
        {
            point.position += delta;
            const bool valid =
                selectionMask.isNull()
                    ? isValidInputStrokePoint(point, current.size)
                    : isValidStoredStrokePoint(point);
            if (!valid)
            {
                return rejectHistoryMutation();
            }
        }
        if (!selectionMask.isNull())
        {
            copy.visibilityClip.reset();
            copy.clipMask = duplicateMask;
        }
        else
        {
            const std::optional<QImage> visibility =
                materializedVisibilityMask(stroke, current.size);
            if (!visibility)
            {
                return rejectHistoryMutation();
            }
            copy.visibilityClip.reset();
            copy.clipMask = *visibility;
            if (!transformMask(
                    copy.clipMask, current.size, transform, transformedMasks))
            {
                return rejectHistoryMutation();
            }
        }
        if (!transformMask(
                copy.fillMask, current.size, transform, transformedMasks))
        {
            return rejectHistoryMutation();
        }
        if (!canonicalizeStrokeVisibility(copy, current.size))
        {
            return rejectHistoryMutation();
        }
        addedPoints += copy.points.size();
        sourceIds.append(stroke.id);
        duplicateIds.append(copy.id);
        copies.append(std::move(copy));
    }
    if (copies.isEmpty()
        || layer->strokes.size()
               > DocumentLimits::maximumStrokesPerLayer - copies.size()
        || totalStrokeCount(current)
               > DocumentLimits::maximumTotalStrokes - copies.size()
        || totalPointCount(current)
               > DocumentLimits::maximumTotalPoints - addedPoints)
    {
        return rejectHistoryMutation();
    }
    Document withCopies = current;
    if (Layer *target = withCopies.layer(layerId))
    {
        target->strokes += copies;
    }
    if (distinctClipMaskBytes(withCopies)
        > DocumentLimits::maximumDistinctClipMaskBytes)
    {
        return rejectHistoryMutation();
    }

    auto effects = std::make_shared<HistoryEffects>();
    effects->beforeDocumentChanged.append(HistoryEffects::StrokeDuplicate{
        layerId, sourceIds, duplicateIds, delta});
    effects->afterDocumentChanged.append(
        HistoryEffects::LayerThumbnail{layerId});
    return tryCommitCandidate(
        tr("Duplicate selection"), std::move(withCopies), std::move(effects));
}

bool DocumentController::removeSelectedContent(const QUuid &layerId,
    const QVector<QUuid> &strokeIds,
    const QImage &selectionMask)
{
    const Document &current = document();
    if (strokeIds.isEmpty() || selectionMask.size() != current.size
        || selectionMask.format() != QImage::Format_Grayscale8)
    {
        return rejectHistoryMutation();
    }

    const Layer *layer = current.layer(layerId);
    if (!layer)
    {
        return rejectHistoryMutation();
    }
    const QSet<QUuid> requested(strokeIds.cbegin(), strokeIds.cend());
    if (layer->strokes.size() >= DocumentLimits::maximumStrokesPerLayer
        || totalStrokeCount(current) >= DocumentLimits::maximumTotalStrokes
        || !std::any_of(layer->strokes.cbegin(),
            layer->strokes.cend(),
            [&requested](const Stroke &stroke)
            {
                return requested.contains(stroke.id);
            })
        || !selectionHasVisibleLayerPixels(layerId, selectionMask))
    {
        return rejectHistoryMutation();
    }
    const std::optional<Stroke> operation =
        selectionOperationStroke(selectionMask, QTransform(), true, false);
    if (!operation)
    {
        return rejectHistoryMutation();
    }
    Document withoutSelection = current;
    withoutSelection.layer(layerId)->strokes.append(*operation);
    if (distinctClipMaskBytes(withoutSelection)
        > DocumentLimits::maximumDistinctClipMaskBytes)
    {
        return rejectHistoryMutation();
    }

    auto effects = std::make_shared<HistoryEffects>();
    effects->afterDocumentChanged.append(
        HistoryEffects::LayerThumbnail{layerId});
    return tryCommitCandidate(tr("Delete selected content"),
        std::move(withoutSelection),
        std::move(effects));
}

bool DocumentController::updateStrokeAttributes(const QUuid &layerId,
    const QVector<QUuid> &strokeIds,
    const std::optional<QColor> &color,
    const std::optional<qreal> &width,
    const std::optional<qreal> &roughness)
{
    const Document &current = document();
    const Layer *layer = current.layer(layerId);
    if (!layer || layer->kind != LayerKind::Paint || strokeIds.isEmpty()
        || (!color && !width && !roughness) || (color && !color->isValid())
        || (width
            && (!std::isfinite(*width)
                || *width < DocumentLimits::minimumStrokeWidth
                || *width > DocumentLimits::maximumStrokeWidth))
        || (roughness
            && (!std::isfinite(*roughness)
                || *roughness < DocumentLimits::minimumBrushWobbleScale
                || *roughness > DocumentLimits::maximumBrushWobbleScale)))
    {
        return rejectHistoryMutation();
    }

    const QSet<QUuid> requested(strokeIds.cbegin(), strokeIds.cend());
    Document candidate = current;
    Layer *target = candidate.layer(layerId);
    bool changed = false;
    for (Stroke &stroke : target->strokes)
    {
        if (!requested.contains(stroke.id))
        {
            continue;
        }
        if (color
            && (stroke.mode == StrokeMode::Paint
                || stroke.mode == StrokeMode::Fill)
            && stroke.color != *color)
        {
            stroke.color = *color;
            changed = true;
        }
        if (width
            && (stroke.mode == StrokeMode::Paint
                || stroke.mode == StrokeMode::Erase)
            && !qFuzzyCompare(stroke.width, *width))
        {
            stroke.width = *width;
            changed = true;
        }
        if (roughness
            && (stroke.mode == StrokeMode::Paint
                || stroke.mode == StrokeMode::Erase)
            && !qFuzzyCompare(stroke.brush.wobbleScale, *roughness))
        {
            stroke.brush.wobbleScale = *roughness;
            changed = true;
        }
    }
    if (!changed)
    {
        return rejectHistoryMutation();
    }

    auto effects = std::make_shared<HistoryEffects>();
    effects->afterDocumentChanged.append(
        HistoryEffects::LayerThumbnail{layerId});
    return tryCommitCandidate(
        tr("Edit stroke properties"), std::move(candidate), std::move(effects));
}

bool DocumentController::transformStrokes(const QUuid &layerId,
    const QVector<QUuid> &strokeIds,
    const QTransform &transform,
    qreal widthScale,
    const QString &text,
    const QImage &selectionMask)
{
    const Document &current = document();
    const Layer *layer = current.layer(layerId);
    if (!layer || strokeIds.isEmpty() || !std::isfinite(widthScale)
        || widthScale <= 0.0)
    {
        return rejectHistoryMutation();
    }
    if (selectionMask.isNull()
        && std::any_of(layer->strokes.cbegin(),
            layer->strokes.cend(),
            [](const Stroke &stroke)
            {
                return stroke.mode == StrokeMode::PixelSelection
                       || stroke.mode == StrokeMode::Reframe;
            }))
    {
        return rejectHistoryMutation();
    }
    bool invertible = false;
    const QTransform inverse = transform.inverted(&invertible);
    if (!invertible)
    {
        return rejectHistoryMutation();
    }

    const QSet<QUuid> requested(strokeIds.cbegin(), strokeIds.cend());
    if (!selectionMask.isNull())
    {
        if (selectionMask.size() != current.size
            || selectionMask.format() != QImage::Format_Grayscale8)
        {
            return rejectHistoryMutation();
        }
        const Layer *sourceLayer = current.layer(layerId);
        if (!sourceLayer
            || sourceLayer->strokes.size()
                   >= DocumentLimits::maximumStrokesPerLayer
            || totalStrokeCount(current) >= DocumentLimits::maximumTotalStrokes
            || !std::any_of(sourceLayer->strokes.cbegin(),
                sourceLayer->strokes.cend(),
                [&requested](const Stroke &stroke)
                {
                    return requested.contains(stroke.id);
                })
            || !selectionHasVisibleLayerPixels(layerId, selectionMask))
        {
            return rejectHistoryMutation();
        }
        const std::optional<Stroke> operation =
            selectionOperationStroke(selectionMask, transform, true, true);
        if (!operation)
        {
            return rejectHistoryMutation();
        }
        Document transformedDocument = current;
        transformedDocument.layer(layerId)->strokes.append(*operation);
        if (distinctClipMaskBytes(transformedDocument)
            > DocumentLimits::maximumDistinctClipMaskBytes)
        {
            return rejectHistoryMutation();
        }
        const QImage nextSelectionMask =
            transformedSelectionSupport(selectionMask,
                current.size,
                transform,
                operation->pixelSelectionOp->sampling);
        if (!maskHasContent(nextSelectionMask))
        {
            return rejectHistoryMutation();
        }
        const PackedMaskRegion sourceMaskSnapshot{
            operation->pixelSelectionOp->canvasSize,
            operation->pixelSelectionOp->sourceBounds,
            operation->pixelSelectionOp->packedMask};
        const std::optional<PackedMaskRegion> nextMaskSnapshot =
            packBinaryMask(nextSelectionMask);
        if (!nextMaskSnapshot)
        {
            return rejectHistoryMutation();
        }
        const QVector<QUuid> sourceIds = strokeIds;
        const QVector<QUuid> resultIds{operation->id};
        auto effects = std::make_shared<HistoryEffects>();
        effects->beforeDocumentChanged.append(
            HistoryEffects::SelectionOverlay{layerId,
                sourceIds,
                resultIds,
                sourceMaskSnapshot,
                *nextMaskSnapshot});
        effects->afterDocumentChanged.append(
            HistoryEffects::LayerThumbnail{layerId});
        return tryCommitCandidate(
            text, std::move(transformedDocument), std::move(effects));
    }

    QVector<Stroke> after;
    QVector<QUuid> transformedIds;
    QHash<qint64, QImage> transformedMasks;
    for (const Stroke &stroke : layer->strokes)
    {
        if (!requested.contains(stroke.id))
        {
            continue;
        }
        if (stroke.mode == StrokeMode::PixelSelection
            || stroke.mode == StrokeMode::Reframe)
        {
            return rejectHistoryMutation();
        }
        Stroke transformed = stroke;
        for (StrokePoint &point : transformed.points)
        {
            point.position = transform.map(point.position);
            if (!isValidInputStrokePoint(point, current.size))
            {
                return rejectHistoryMutation();
            }
        }
        transformed.width = std::clamp(transformed.width * widthScale,
            DocumentLimits::minimumStrokeWidth,
            DocumentLimits::maximumStrokeWidth);
        if (!transformMask(transformed.clipMask,
                current.size,
                transform,
                transformedMasks))
        {
            return rejectHistoryMutation();
        }
        if (!transformMask(transformed.fillMask,
                current.size,
                transform,
                transformedMasks))
        {
            return rejectHistoryMutation();
        }
        if (!canonicalizeStrokeVisibility(transformed, current.size))
        {
            return rejectHistoryMutation();
        }
        after.append(std::move(transformed));
        transformedIds.append(stroke.id);
    }
    if (after.isEmpty())
    {
        return rejectHistoryMutation();
    }
    Document transformedDocument = current;
    if (Layer *target = transformedDocument.layer(layerId))
    {
        QHash<QUuid, Stroke> replacements;
        for (const Stroke &stroke : after)
        {
            replacements.insert(stroke.id, stroke);
        }
        for (Stroke &stroke : target->strokes)
        {
            const auto replacement = replacements.constFind(stroke.id);
            if (replacement != replacements.cend())
            {
                stroke = replacement.value();
            }
        }
    }
    if (distinctClipMaskBytes(transformedDocument)
        > DocumentLimits::maximumDistinctClipMaskBytes)
    {
        return rejectHistoryMutation();
    }

    auto effects = std::make_shared<HistoryEffects>();
    effects->beforeDocumentChanged.append(HistoryEffects::StrokeTransform{
        layerId, transformedIds, transform, inverse});
    effects->afterDocumentChanged.append(
        HistoryEffects::LayerThumbnail{layerId});
    return tryCommitCandidate(
        text, std::move(transformedDocument), std::move(effects));
}

void DocumentController::removeStrokes(
    const QUuid &layerId, const QVector<QUuid> &strokeIds)
{
    const Document &current = document();
    const Layer *layer = current.layer(layerId);
    if (!layer || strokeIds.isEmpty())
    {
        failHistoryMacro();
        return;
    }

    const QSet<QUuid> requested(strokeIds.cbegin(), strokeIds.cend());
    QSet<QUuid> removableIds;
    for (const Stroke &stroke : layer->strokes)
    {
        if (requested.contains(stroke.id)
            && stroke.mode != StrokeMode::PixelSelection
            && stroke.mode != StrokeMode::Reframe)
        {
            removableIds.insert(stroke.id);
        }
    }
    if (removableIds.isEmpty())
    {
        failHistoryMacro();
        return;
    }

    Document candidate = current;
    candidate.layer(layerId)->strokes.removeIf(
        [&removableIds](const Stroke &stroke)
        {
            return removableIds.contains(stroke.id);
        });
    auto effects = std::make_shared<HistoryEffects>();
    effects->afterDocumentChanged.append(
        HistoryEffects::LayerThumbnail{layerId});
    tryCommitCandidate(
        tr("Delete selection"), std::move(candidate), std::move(effects));
}

void DocumentController::addLayer(const QUuid &parentGroupId)
{
    const Document &current = document();
    const Layer *requestedGroup = current.layer(parentGroupId);
    if (current.layers.size() >= DocumentLimits::maximumLayers
        || (!parentGroupId.isNull()
            && (!requestedGroup || requestedGroup->kind != LayerKind::Group)))
    {
        failHistoryMacro();
        return;
    }
    Layer layer;
    layer.name = nextLayerName();
    layer.initialCanvasSize = current.size;
    if (requestedGroup)
    {
        layer.parentGroupId = requestedGroup->id;
    }
    else if (const Layer *active = current.layer(current.activeLayerId))
    {
        layer.parentGroupId = active->parentGroupId;
    }
    const QUuid layerId = layer.id;
    Document candidate = current;
    const int activeIndex = current.layerIndex(current.activeLayerId);
    if (requestedGroup)
    {
        candidate.layers.append(std::move(layer));
    }
    else if (activeIndex >= 0)
    {
        candidate.layers.insert(activeIndex + 1, std::move(layer));
    }
    else
    {
        candidate.layers.append(std::move(layer));
    }
    candidate.activeLayerId = layerId;
    auto effects = std::make_shared<HistoryEffects>();
    effects->afterDocumentChanged.append(
        HistoryEffects::LayerThumbnail{layerId});
    effects->afterDocumentChanged.append(HistoryEffects::ActiveLayer{});
    tryCommitCandidate(tr("Add layer"),
        std::move(candidate),
        std::move(effects),
        ActiveLayerPolicy::UsePrepared);
}

void DocumentController::addLayerGroup(const QUuid &childId)
{
    const Document &current = document();
    if (current.layers.size() >= DocumentLimits::maximumLayers)
    {
        failHistoryMacro();
        return;
    }
    const Layer *child = current.layer(childId);
    Layer group;
    group.kind = LayerKind::Group;
    group.initialCanvasSize = current.size;
    group.parentGroupId = child ? child->parentGroupId : QUuid();
    int number = 1;
    do
    {
        group.name = tr("Group %1").arg(number++);
    } while (std::any_of(current.layers.cbegin(),
        current.layers.cend(),
        [&group](const Layer &layer)
        {
            return layer.name == group.name;
        }));

    Document candidate = current;
    const int childIndex = current.layerIndex(childId);
    if (childIndex >= 0)
    {
        candidate.layers.insert(childIndex + 1, group);
        candidate.layer(childId)->parentGroupId = group.id;
    }
    else
    {
        candidate.layers.append(group);
    }
    auto effects = std::make_shared<HistoryEffects>();
    effects->afterDocumentChanged.append(
        HistoryEffects::LayerThumbnailsReset{});
    tryCommitCandidate(
        tr("Add layer group"), std::move(candidate), std::move(effects));
}

void DocumentController::duplicateLayer(const QUuid &id)
{
    const Document &current = document();
    const int sourceIndex = current.layerIndex(id);
    if (sourceIndex < 0 || current.layers[sourceIndex].kind != LayerKind::Paint
        || current.layers.size() >= DocumentLimits::maximumLayers)
    {
        failHistoryMacro();
        return;
    }
    const qsizetype sourcePointCount =
        layerPointCount(current.layers[sourceIndex]);
    const qsizetype existingPointCount = totalPointCount(current);
    const qsizetype sourceStrokeCount =
        current.layers[sourceIndex].strokes.size();
    const qsizetype existingStrokeCount = totalStrokeCount(current);
    if (sourcePointCount > DocumentLimits::maximumTotalPoints
        || existingPointCount > DocumentLimits::maximumTotalPoints
        || sourcePointCount
               > DocumentLimits::maximumTotalPoints - existingPointCount
        || sourceStrokeCount > DocumentLimits::maximumTotalStrokes
        || existingStrokeCount > DocumentLimits::maximumTotalStrokes
        || sourceStrokeCount
               > DocumentLimits::maximumTotalStrokes - existingStrokeCount)
    {
        failHistoryMacro();
        return;
    }
    Layer copy = current.layers[sourceIndex];
    copy.id = QUuid::createUuid();
    copy.name = tr("%1 copy").arg(copy.name);
    if (copy.name.size() > DocumentLimits::maximumLayerNameLength)
    {
        copy.name.truncate(DocumentLimits::maximumLayerNameLength);
    }
    for (Stroke &stroke : copy.strokes)
    {
        stroke.id = QUuid::createUuid();
    }
    Document withCopy = current;
    withCopy.layers.insert(sourceIndex + 1, copy);
    withCopy.activeLayerId = copy.id;
    if (distinctClipMaskBytes(withCopy)
        > DocumentLimits::maximumDistinctClipMaskBytes)
    {
        failHistoryMacro();
        return;
    }
    auto effects = std::make_shared<HistoryEffects>();
    effects->afterDocumentChanged.append(
        HistoryEffects::LayerThumbnailsReset{});
    effects->afterDocumentChanged.append(HistoryEffects::ActiveLayer{});
    tryCommitCandidate(tr("Duplicate layer"),
        std::move(withCopy),
        std::move(effects),
        ActiveLayerPolicy::UsePrepared);
}

void DocumentController::removeLayer(const QUuid &id)
{
    const Document &current = document();
    const int index = current.layerIndex(id);
    if (index < 0)
    {
        failHistoryMacro();
        return;
    }
    Document candidate = current;
    QSet<QUuid> removedIds{id};
    if (current.layers[index].kind == LayerKind::Group)
    {
        const LayerHierarchyAnalysis hierarchy = analyzeLayerHierarchy(current);
        if (!hierarchy.isValid())
        {
            failHistoryMacro();
            return;
        }
        for (const Layer &layer : current.layers)
        {
            if (hierarchy.isDescendantOf(layer.id, id))
            {
                removedIds.insert(layer.id);
            }
        }
    }
    if (removedIds.contains(candidate.activeLayerId))
    {
        candidate.activeLayerId = {};
        const auto choosePaint = [&](int begin, int end, int step)
        {
            for (int candidateIndex = begin; candidateIndex != end;
                candidateIndex += step)
            {
                const Layer &candidateLayer = current.layers[candidateIndex];
                if (!removedIds.contains(candidateLayer.id)
                    && candidateLayer.kind == LayerKind::Paint)
                {
                    candidate.activeLayerId = candidateLayer.id;
                    return true;
                }
            }
            return false;
        };
        if (!choosePaint(index - 1, -1, -1))
        {
            choosePaint(index + 1, current.layers.size(), 1);
        }
    }
    candidate.layers.removeIf(
        [&removedIds](const Layer &layer)
        {
            return removedIds.contains(layer.id);
        });
    ensureActiveLayer(candidate);
    auto effects = std::make_shared<HistoryEffects>();
    effects->afterDocumentChanged.append(
        HistoryEffects::LayerThumbnailsReset{});
    effects->afterDocumentChanged.append(HistoryEffects::ActiveLayer{});
    tryCommitCandidate(tr("Delete layer"),
        std::move(candidate),
        std::move(effects),
        ActiveLayerPolicy::UsePrepared);
}

void DocumentController::clearLayer(const QUuid &id)
{
    const Document &current = document();
    const Layer *layer = current.layer(id);
    if (!layer || layer->kind != LayerKind::Paint
        || (layer->strokes.isEmpty()
            && layer->initialCanvasSize == current.size))
    {
        failHistoryMacro();
        return;
    }
    Document candidate = current;
    Layer *target = candidate.layer(id);
    target->strokes.clear();
    target->initialCanvasSize = current.size;
    auto effects = std::make_shared<HistoryEffects>();
    effects->afterDocumentChanged.append(HistoryEffects::LayerThumbnail{id});
    tryCommitCandidate(
        tr("Clear layer"), std::move(candidate), std::move(effects));
}

DocumentController::RenameLayerResult DocumentController::renameLayer(
    const QUuid &id, const QString &name)
{
    const auto reject = [this](RenameLayerResult result)
    {
        failHistoryMacro();
        return result;
    };
    const Document &current = document();
    const Layer *layer = current.layer(id);
    const QString normalized = name.trimmed();
    if (!layer)
    {
        return reject(RenameLayerResult::RejectedInvalidLayer);
    }
    if (normalized.isEmpty())
    {
        return reject(RenameLayerResult::RejectedEmptyName);
    }
    if (normalized.size() > DocumentLimits::maximumLayerNameLength)
    {
        return reject(RenameLayerResult::RejectedNameTooLong);
    }
    if (layer->name == normalized)
    {
        return reject(RenameLayerResult::Unchanged);
    }
    Document candidate = current;
    candidate.layer(id)->name = normalized;
    return tryCommitCandidate(tr("Rename layer"), std::move(candidate))
               ? RenameLayerResult::Renamed
               : RenameLayerResult::RejectedCommit;
}

void DocumentController::setLayerVisible(const QUuid &id, bool visible)
{
    const Document &current = document();
    const Layer *layer = current.layer(id);
    if (!layer || layer->visible == visible)
    {
        failHistoryMacro();
        return;
    }
    Document candidate = current;
    candidate.layer(id)->visible = visible;
    auto effects = std::make_shared<HistoryEffects>();
    effects->afterDocumentChanged.append(
        HistoryEffects::LayerThumbnailsReset{});
    tryCommitCandidate(tr("Toggle layer visibility"),
        std::move(candidate),
        std::move(effects));
}

void DocumentController::setLayerReference(const QUuid &id, bool reference)
{
    const Document &current = document();
    const Layer *layer = current.layer(id);
    if (!layer || layer->kind != LayerKind::Paint
        || layer->reference == reference)
    {
        failHistoryMacro();
        return;
    }
    Document candidate = current;
    candidate.layer(id)->reference = reference;
    tryCommitCandidate(tr("Set reference layer"), std::move(candidate));
}

void DocumentController::setLayerOpacity(const QUuid &id, qreal opacity)
{
    const Document &current = document();
    const Layer *layer = current.layer(id);
    if (!std::isfinite(opacity))
    {
        failHistoryMacro();
        return;
    }
    const qreal normalized = std::clamp(opacity, 0.0, 1.0);
    if (!layer || qFuzzyCompare(layer->opacity, normalized))
    {
        failHistoryMacro();
        return;
    }
    Document candidate = current;
    candidate.layer(id)->opacity = normalized;
    auto effects = std::make_shared<HistoryEffects>();
    effects->afterDocumentChanged.append(
        HistoryEffects::LayerThumbnailsReset{});
    tryCommitCandidate(tr("Change layer opacity"),
        std::move(candidate),
        std::move(effects),
        ActiveLayerPolicy::PreserveCurrentIfPresent,
        layerOpacityMergeId,
        id);
}

void DocumentController::setLayerBlendMode(const QUuid &id, LayerBlendMode mode)
{
    const Document &current = document();
    const Layer *layer = current.layer(id);
    if (!layer || !isValidLayerBlendMode(mode) || layer->blendMode == mode)
    {
        failHistoryMacro();
        return;
    }
    Document candidate = current;
    candidate.layer(id)->blendMode = mode;
    auto effects = std::make_shared<HistoryEffects>();
    effects->afterDocumentChanged.append(
        HistoryEffects::LayerThumbnailsReset{});
    tryCommitCandidate(tr("Change layer blend mode"),
        std::move(candidate),
        std::move(effects));
}

void DocumentController::setLayerClipToBelow(const QUuid &id, bool clipped)
{
    const Document &current = document();
    const Layer *layer = current.layer(id);
    if (!layer || layer->kind != LayerKind::Paint
        || layer->clipToLayerBelow == clipped)
    {
        failHistoryMacro();
        return;
    }
    Document candidate = current;
    candidate.layer(id)->clipToLayerBelow = clipped;
    auto effects = std::make_shared<HistoryEffects>();
    effects->afterDocumentChanged.append(
        HistoryEffects::LayerThumbnailsReset{});
    tryCommitCandidate(
        tr("Change layer clipping"), std::move(candidate), std::move(effects));
}

void DocumentController::setLayerParentGroup(
    const QUuid &id, const QUuid &groupId)
{
    const Document &current = document();
    const Layer *layer = current.layer(id);
    const Layer *group = current.layer(groupId);
    const bool validParent =
        groupId.isNull() || (group && group->kind == LayerKind::Group);
    if (!layer || !validParent || layer->parentGroupId == groupId
        || id == groupId)
    {
        failHistoryMacro();
        return;
    }
    Document candidate = current;
    candidate.layer(id)->parentGroupId = groupId;
    auto effects = std::make_shared<HistoryEffects>();
    effects->afterDocumentChanged.append(
        HistoryEffects::LayerThumbnailsReset{});
    tryCommitCandidate(
        tr("Move layer into group"), std::move(candidate), std::move(effects));
}

void DocumentController::moveLayer(const QUuid &id, int offset)
{
    const Document &current = document();
    const int from = current.layerIndex(id);
    const Layer *layer = current.layer(id);
    if (from < 0 || !layer || offset == 0)
    {
        failHistoryMacro();
        return;
    }
    QVector<int> siblingIndexes;
    for (int index = 0; index < current.layers.size(); ++index)
    {
        if (current.layers[index].parentGroupId == layer->parentGroupId)
        {
            siblingIndexes.append(index);
        }
    }
    const int siblingPosition = siblingIndexes.indexOf(from);
    const int targetPosition = siblingPosition + offset;
    if (siblingPosition < 0 || targetPosition < 0
        || targetPosition >= siblingIndexes.size())
    {
        failHistoryMacro();
        return;
    }
    const int to = siblingIndexes[targetPosition];
    Document candidate = current;
    candidate.layers.move(from, to);
    auto effects = std::make_shared<HistoryEffects>();
    effects->afterDocumentChanged.append(
        HistoryEffects::LayerThumbnailsReset{});
    tryCommitCandidate(
        tr("Move layer"), std::move(candidate), std::move(effects));
}

void DocumentController::setWobbleAmount(qreal amount)
{
    if (!std::isfinite(amount))
    {
        failHistoryMacro();
        return;
    }
    const qreal normalized = std::clamp(amount,
        DocumentLimits::minimumWobbleAmount,
        DocumentLimits::maximumWobbleAmount);
    const Document &current = document();
    if (qFuzzyCompare(current.wobbleAmount, normalized))
    {
        failHistoryMacro();
        return;
    }
    Document candidate = current;
    candidate.wobbleAmount = normalized;
    tryCommitCandidate(tr("Change wobble"),
        std::move(candidate),
        {},
        ActiveLayerPolicy::PreserveCurrentIfPresent,
        wobbleAmountMergeId);
}

void DocumentController::setAnimationFrames(int frames)
{
    const int normalized = std::clamp(frames,
        DocumentLimits::minimumAnimationFrames,
        DocumentLimits::maximumAnimationFrames);
    const Document &current = document();
    if (current.animationFrames == normalized)
    {
        failHistoryMacro();
        return;
    }
    Document candidate = current;
    candidate.animationFrames = normalized;
    tryCommitCandidate(tr("Change animation frames"),
        std::move(candidate),
        {},
        ActiveLayerPolicy::PreserveCurrentIfPresent,
        animationFramesMergeId);
}

void DocumentController::setFramesPerSecond(qreal fps)
{
    if (!std::isfinite(fps))
    {
        failHistoryMacro();
        return;
    }
    const qreal normalized = std::clamp(fps,
        DocumentLimits::minimumFramesPerSecond,
        DocumentLimits::maximumFramesPerSecond);
    const Document &current = document();
    if (qFuzzyCompare(current.framesPerSecond, normalized))
    {
        failHistoryMacro();
        return;
    }
    Document candidate = current;
    candidate.framesPerSecond = normalized;
    tryCommitCandidate(tr("Change animation speed"),
        std::move(candidate),
        {},
        ActiveLayerPolicy::PreserveCurrentIfPresent,
        framesPerSecondMergeId);
}

bool DocumentController::tryCommitCandidate(QString text,
    Document candidate,
    std::shared_ptr<const HistoryEffects> effects,
    ActiveLayerPolicy activeLayerPolicy,
    int mergeId,
    const QUuid &mergeScope)
{
    const PreparedState before = editableState();
    if (!before || m_undoStack.m_moving
        || (m_macroTransaction && m_macroTransaction->failed))
    {
        return false;
    }
    const LayerHierarchyAnalysis currentHierarchy =
        analyzeLayerHierarchy(before->document());
    const LayerHierarchyAnalysis candidateHierarchy =
        analyzeLayerHierarchy(candidate);
    if (!isLayerHierarchyDepthChangeAllowed(
            currentHierarchy, candidateHierarchy))
    {
        failHistoryMacro();
        return false;
    }
    const PreparedState after =
        prepareState(std::move(candidate), before.get());
    if (!after)
    {
        failHistoryMacro();
        return false;
    }
    return tryCommitPreparedCandidate(std::move(text),
        before,
        after,
        std::move(effects),
        activeLayerPolicy,
        mergeId,
        mergeScope);
}

bool DocumentController::tryCommitPreparedCandidate(QString text,
    const PreparedState &before,
    PreparedState after,
    std::shared_ptr<const HistoryEffects> effects,
    ActiveLayerPolicy activeLayerPolicy,
    int mergeId,
    const QUuid &mergeScope,
    const QUuid &appendedStrokeLayerId)
{
    if (!before || !after || before != editableState() || m_undoStack.m_moving
        || (m_macroTransaction && m_macroTransaction->failed))
    {
        failHistoryMacro();
        return false;
    }
    if (!effects)
    {
        effects = std::make_shared<const HistoryEffects>();
    }
    else
    {
        effects = std::make_shared<const HistoryEffects>(effects->frozenCopy());
    }
    DocumentDelta delta =
        appendedStrokeLayerId.isNull()
            ? DocumentDelta::between(before->document(), after->document())
            : DocumentDelta::appendedStroke(
                  before->document(), after->document(), appendedStrokeLayerId);
    if (delta.isEmpty())
    {
        failHistoryMacro();
        return false;
    }

    if (m_macroTransaction)
    {
        m_macroTransaction->workingState = after;
        m_macroTransaction->effects.append(*effects);
        return true;
    }

    const quint64 beforeNode = m_currentHistoryNode;
    const quint64 beforeRevision = m_currentContentRevision;
    const QVector<Stroke> beforePayload = delta.payloadStrokes(false);
    const QVector<Stroke> afterPayload = delta.payloadStrokes(true);
    DocumentSerializer::ImmutableBackingLease beforeLease =
        DocumentSerializer::retainImmutableBackings(*before, beforePayload);
    DocumentSerializer::ImmutableBackingLease afterLease =
        DocumentSerializer::retainImmutableBackings(*after, afterPayload);
    if (!beforeLease.isValid() || !afterLease.isValid())
    {
        failHistoryMacro();
        return false;
    }
    const quint64 afterNode = m_nextHistoryNode + 1;
    const quint64 afterRevision = m_nextContentRevision + 1;
    m_nextHistoryNode = afterNode;
    m_nextContentRevision = afterRevision;
    m_undoStack.push(new DocumentCommand(this,
        std::move(text),
        std::move(delta),
        after,
        activeLayerPolicy,
        std::move(effects),
        std::move(beforeLease),
        std::move(afterLease),
        mergeId,
        mergeScope,
        beforeNode,
        afterNode,
        beforeRevision,
        afterRevision,
        before->compactSize(),
        after->compactSize()));
    return true;
}

DocumentController::PreparedState DocumentController::prepareState(
    Document document,
    const PreparedDocument *base,
    const DocumentSerializer::ImmutableBackingLease *trusted,
    bool historyPreflight,
    QString *error)
{
    if (historyPreflight && m_historyPrepareFailureCountdownForTesting == 0)
    {
        m_historyPrepareFailureCountdownForTesting = -1;
        return {};
    }
    if (historyPreflight && m_historyPrepareFailureCountdownForTesting > 0)
    {
        --m_historyPrepareFailureCountdownForTesting;
    }
    const PreparedDocument *effectiveBase = base;
    if (!effectiveBase && m_currentState)
    {
        effectiveBase = m_currentState.get();
    }
    std::optional<PreparedDocument> prepared =
        DocumentSerializer::prepare(std::move(document),
            m_serializationCache,
            effectiveBase,
            trusted,
            DocumentLimits::maximumProjectBytes,
            error);
    if (!prepared)
    {
        return {};
    }
    return std::make_shared<const PreparedDocument>(std::move(*prepared));
}

void DocumentController::applyPreparedState(const PreparedState &state,
    ActiveLayerPolicy activeLayerPolicy,
    const HistoryEffects &effects,
    CommitDirection direction,
    quint64 historyNode,
    quint64 contentRevision)
{
    Q_ASSERT(state && state->isValid());
    if (!state || !state->isValid())
    {
        return;
    }

    PreparedState appliedState = state;
    if (activeLayerPolicy == ActiveLayerPolicy::PreserveCurrentIfPresent
        && m_currentState)
    {
        const QUuid currentActive = document().activeLayerId;
        const Document &target = state->document();
        const bool canPreserve = target.layers.isEmpty()
                                     ? currentActive.isNull()
                                     : target.layer(currentActive) != nullptr;
        if (canPreserve && target.activeLayerId != currentActive)
        {
            std::optional<PreparedDocument> rebound =
                DocumentSerializer::rebindActiveLayer(*state, currentActive);
            Q_ASSERT(rebound.has_value());
            if (rebound)
            {
                appliedState = std::make_shared<const PreparedDocument>(
                    std::move(*rebound));
            }
        }
    }

    const bool wasModified = isModified();
    // Install the complete observable controller state before any callback.
    // Slots may query or modify the controller synchronously from the
    // command's effect and documentChanged signals.
    m_currentState = std::move(appliedState);
    m_currentHistoryNode = historyNode;
    m_currentContentRevision = contentRevision;
    m_selectionVisibilityCacheValid = false;
    dispatchHistoryEffects(effects, direction, true);
    const bool modified = isModified();
    if (modified != wasModified)
    {
        emit modifiedChanged(modified);
    }
}

bool DocumentController::preflightHistoryMovement(
    const QUndoCommand *command, bool forward)
{
    if (!command || !m_currentState)
    {
        return false;
    }
    auto *logical = const_cast<LogicalHistoryCommand *>(
        dynamic_cast<const LogicalHistoryCommand *>(command));
    return logical && logical->preflight(forward);
}

void DocumentController::applyHistoryMovement(
    QUndoCommand *command, bool forward)
{
    if (!command)
    {
        return;
    }
    if (forward)
    {
        command->redo();
    }
    else
    {
        command->undo();
    }
}

void DocumentController::clearHistoryPreflight(const QUndoCommand *command)
{
    auto *logical = const_cast<LogicalHistoryCommand *>(
        dynamic_cast<const LogicalHistoryCommand *>(command));
    if (logical)
    {
        logical->clearPreflight();
    }
}

DocumentUndoStack::StorageStats DocumentController::historyStorageStats(
    const QUndoCommand *command) const
{
    DocumentUndoStack::StorageStats total;
    if (!command)
    {
        return total;
    }
    if (const auto *logical =
            dynamic_cast<const LogicalHistoryCommand *>(command))
    {
        return logical->storageStats();
    }
    return total;
}

void DocumentController::dispatchHistoryEffects(const HistoryEffects &effects,
    CommitDirection direction,
    bool changedDocument)
{
    const bool forward = direction == CommitDirection::Forward;
    for (const HistoryEffects::BeforeEvent &event :
        effects.beforeDocumentChanged)
    {
        std::visit(
            [this, forward](const auto &value)
            {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, HistoryEffects::CanvasResize>)
                {
                    emit canvasResized(
                        forward ? value.beforeSize : value.afterSize,
                        forward ? value.afterSize : value.beforeSize,
                        forward ? value.forwardTransform
                                : value.reverseTransform);
                }
                else if constexpr (std::is_same_v<T,
                                       HistoryEffects::StrokeTransform>)
                {
                    emit strokesTransformed(value.layerId,
                        value.strokeIds,
                        forward ? value.forwardTransform
                                : value.reverseTransform);
                }
                else if constexpr (std::is_same_v<T,
                                       HistoryEffects::StrokeDuplicate>)
                {
                    emit strokesDuplicated(value.layerId,
                        value.sourceIds,
                        value.duplicateIds,
                        value.delta,
                        forward);
                }
                else if constexpr (std::is_same_v<T,
                                       HistoryEffects::SelectionOverlay>)
                {
                    const auto unpack = [](const auto &mask)
                    {
                        return mask ? unpackBinaryMask(*mask) : QImage();
                    };
                    emit selectionOverlayTransition(value.layerId,
                        forward ? value.beforeIds : value.afterIds,
                        forward ? value.afterIds : value.beforeIds,
                        unpack(forward ? value.beforeMask : value.afterMask),
                        unpack(forward ? value.afterMask : value.beforeMask));
                }
                else if constexpr (std::is_same_v<T,
                                       HistoryEffects::StrokePresence>)
                {
                    emit strokePresenceChanged(value.layerId,
                        value.strokeId,
                        value.clipMask ? unpackBinaryMask(*value.clipMask)
                                       : QImage(),
                        forward);
                }
            },
            event);
    }
    if (changedDocument)
    {
        notifyDocumentChanged();
    }
    for (const HistoryEffects::AfterEvent &event : effects.afterDocumentChanged)
    {
        std::visit(
            [this](const auto &value)
            {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, HistoryEffects::LayerThumbnail>)
                {
                    emit layerThumbnailChanged(value.layerId);
                }
                else if constexpr (std::is_same_v<T,
                                       HistoryEffects::LayerThumbnailsReset>)
                {
                    emit layerThumbnailsReset();
                }
                else if constexpr (std::is_same_v<T,
                                       HistoryEffects::ActiveLayer>)
                {
                    emit activeLayerChanged(document().activeLayerId);
                }
            },
            event);
    }
    if (effects.selectionState)
    {
        const HistoryEffects::SelectionState &state =
            forward ? effects.selectionState->after
                    : effects.selectionState->before;
        emit selectionHistoryStateRequested(state.layerId,
            state.mask ? unpackBinaryMask(*state.mask) : QImage());
    }
}

void DocumentController::beginHistoryMacro(const QString &text)
{
    if (m_undoStack.m_moving || !m_currentState)
    {
        return;
    }
    if (m_macroTransaction)
    {
        ++m_macroTransaction->depth;
        return;
    }
    m_macroTransaction = std::make_unique<MacroTransaction>();
    m_macroTransaction->text = text;
    m_macroTransaction->startState = m_currentState;
    m_macroTransaction->workingState = m_currentState;
}

void DocumentController::endHistoryMacro()
{
    if (!m_macroTransaction || m_undoStack.m_moving)
    {
        return;
    }
    if (--m_macroTransaction->depth > 0)
    {
        return;
    }
    std::unique_ptr<MacroTransaction> transaction =
        std::move(m_macroTransaction);
    if (transaction->failed || !transaction->startState
        || !transaction->workingState)
    {
        return;
    }

    DocumentDelta delta =
        DocumentDelta::between(transaction->startState->document(),
            transaction->workingState->document());
    if (delta.isEmpty())
    {
        transaction->effects.discardDocumentEffects();
        if (!transaction->effects.hasSelectionTransition())
        {
            return;
        }
        auto effects = std::make_shared<const HistoryEffects>(
            transaction->effects.frozenCopy());
        m_undoStack.push(
            new TransientCommand(this, transaction->text, std::move(effects)));
        return;
    }

    const quint64 beforeNode = m_currentHistoryNode;
    const quint64 beforeRevision = m_currentContentRevision;
    const QVector<Stroke> beforePayload = delta.payloadStrokes(false);
    const QVector<Stroke> afterPayload = delta.payloadStrokes(true);
    auto beforeLease = DocumentSerializer::retainImmutableBackings(
        *transaction->startState, beforePayload);
    auto afterLease = DocumentSerializer::retainImmutableBackings(
        *transaction->workingState, afterPayload);
    if (!beforeLease.isValid() || !afterLease.isValid())
    {
        return;
    }
    const quint64 afterNode = m_nextHistoryNode + 1;
    const quint64 afterRevision = m_nextContentRevision + 1;
    m_nextHistoryNode = afterNode;
    m_nextContentRevision = afterRevision;
    auto effects = std::make_shared<const HistoryEffects>(
        transaction->effects.frozenCopy());
    m_undoStack.push(new DocumentCommand(this,
        transaction->text,
        std::move(delta),
        transaction->workingState,
        ActiveLayerPolicy::UsePrepared,
        std::move(effects),
        std::move(beforeLease),
        std::move(afterLease),
        -1,
        {},
        beforeNode,
        afterNode,
        beforeRevision,
        afterRevision,
        transaction->startState->compactSize(),
        transaction->workingState->compactSize()));
}

void DocumentController::failHistoryMacro()
{
    if (m_macroTransaction)
    {
        m_macroTransaction->failed = true;
    }
}

bool DocumentController::rejectHistoryMutation()
{
    failHistoryMacro();
    return false;
}

bool DocumentController::hasOpenHistoryMacro() const
{
    return static_cast<bool>(m_macroTransaction);
}

const DocumentController::PreparedState &
DocumentController::editableState() const
{
    if (m_macroTransaction && m_macroTransaction->workingState)
    {
        return m_macroTransaction->workingState;
    }
    return m_currentState;
}

void DocumentController::normalizeMergedNoOp(
    quint64 historyNode, quint64 contentRevision)
{
    const bool wasModified = isModified();
    m_currentHistoryNode = historyNode;
    m_currentContentRevision = contentRevision;
    const bool modified = isModified();
    if (modified != wasModified)
    {
        emit modifiedChanged(modified);
    }
}

void DocumentController::notifyDocumentChanged()
{
    m_selectionVisibilityCacheValid = false;
    emit documentChanged();
}

void DocumentController::ensureActiveLayer(Document &document)
{
    const Layer *active = document.layer(document.activeLayerId);
    if (active && active->kind == LayerKind::Paint)
    {
        return;
    }
    if (document.layers.isEmpty())
    {
        document.activeLayerId = {};
        return;
    }
    for (auto layer = document.layers.crbegin();
        layer != document.layers.crend();
        ++layer)
    {
        if (layer->kind == LayerKind::Paint)
        {
            document.activeLayerId = layer->id;
            return;
        }
    }
    document.activeLayerId = {};
}

QString DocumentController::nextLayerName() const
{
    const Document &current = document();
    int number = current.layers.size() + 1;
    while (true)
    {
        const QString candidate = tr("Layer %1").arg(number);
        bool exists = false;
        for (const Layer &layer : current.layers)
        {
            if (layer.name == candidate)
            {
                exists = true;
                break;
            }
        }
        if (!exists)
        {
            return candidate;
        }
        ++number;
    }
}

}
