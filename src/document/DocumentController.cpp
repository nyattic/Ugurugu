#include "document/DocumentController.hpp"

#include "document/DocumentLimits.hpp"
#include "document/StrokeMask.hpp"

#include <QHash>
#include <QPainter>
#include <QSet>
#include <QUndoCommand>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <utility>

namespace wobble {

namespace {

class LambdaCommand final : public QUndoCommand
{
public:
    LambdaCommand(
        QString text,
        std::function<void()> redoAction,
        std::function<void()> undoAction,
        int mergeId = -1,
        QUuid mergeScope = QUuid())
        : QUndoCommand(std::move(text))
        , m_redoAction(std::move(redoAction))
        , m_undoAction(std::move(undoAction))
        , m_mergeId(mergeId)
        , m_mergeScope(mergeScope)
    {
    }

    int id() const override
    {
        return m_mergeId;
    }

    bool mergeWith(const QUndoCommand *other) override
    {
        const auto *command = dynamic_cast<const LambdaCommand *>(other);
        if (!command
            || command->m_mergeId != m_mergeId
            || command->m_mergeScope != m_mergeScope) {
            return false;
        }
        m_redoAction = command->m_redoAction;
        return true;
    }

    void redo() override
    {
        m_redoAction();
    }

    void undo() override
    {
        m_undoAction();
    }

private:
    std::function<void()> m_redoAction;
    std::function<void()> m_undoAction;
    int m_mergeId = -1;
    QUuid m_mergeScope;
};

constexpr int wobbleAmountMergeId = 1;
constexpr int animationFramesMergeId = 2;
constexpr int framesPerSecondMergeId = 3;
constexpr int layerOpacityMergeId = 4;

qsizetype totalPointCount(const Document &document)
{
    qsizetype count = 0;
    for (const Layer &layer : document.layers) {
        for (const Stroke &stroke : layer.strokes) {
            if (stroke.points.size()
                > DocumentLimits::maximumTotalPoints - count) {
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
    for (const Layer &layer : document.layers) {
        if (layer.strokes.size()
            > DocumentLimits::maximumTotalStrokes - count) {
            return DocumentLimits::maximumTotalStrokes + 1;
        }
        count += layer.strokes.size();
    }
    return count;
}

qsizetype layerPointCount(const Layer &layer)
{
    qsizetype count = 0;
    for (const Stroke &stroke : layer.strokes) {
        if (stroke.points.size()
            > DocumentLimits::maximumTotalPoints - count) {
            return DocumentLimits::maximumTotalPoints + 1;
        }
        count += stroke.points.size();
    }
    return count;
}

bool containsStrokeId(const Document &document, const QUuid &id)
{
    for (const Layer &layer : document.layers) {
        for (const Stroke &stroke : layer.strokes) {
            if (stroke.id == id) {
                return true;
            }
        }
    }
    return false;
}

bool isValidStrokePoint(const StrokePoint &point, const QSize &size)
{
    return std::isfinite(point.position.x())
        && std::isfinite(point.position.y())
        && std::isfinite(point.pressure)
        && point.position.x() >= 0.0
        && point.position.y() >= 0.0
        && point.position.x() <= size.width()
        && point.position.y() <= size.height()
        && point.pressure >= 0.0
        && point.pressure <= 1.0;
}

quint64 distinctClipMaskBytes(const Document &document)
{
    QSet<qint64> seen;
    quint64 bytes = 0;
    for (const Layer &layer : document.layers) {
        for (const Stroke &stroke : layer.strokes) {
            if (stroke.clipMask.isNull()
                || seen.contains(stroke.clipMask.cacheKey())) {
                continue;
            }
            seen.insert(stroke.clipMask.cacheKey());
            const quint64 maskBytes = stroke.clipMask.sizeInBytes();
            if (maskBytes
                > DocumentLimits::maximumDistinctClipMaskBytes - bytes) {
                return DocumentLimits::maximumDistinctClipMaskBytes + 1;
            }
            bytes += maskBytes;
        }
    }
    return bytes;
}

}

DocumentController::DocumentController(QObject *parent)
    : QObject(parent)
    , m_document(Document::createDefault())
{
    m_undoStack.setUndoLimit(64);
    m_undoStack.setClean();
}

const Document &DocumentController::document() const
{
    return m_document;
}

QUndoStack *DocumentController::undoStack()
{
    return &m_undoStack;
}

bool DocumentController::isModified() const
{
    return m_currentContentRevision != m_savedContentRevision;
}

void DocumentController::pushTransientCommand(
    const QString &text,
    std::function<void()> redoAction,
    std::function<void()> undoAction)
{
    m_undoStack.push(new LambdaCommand(
        text,
        std::move(redoAction),
        std::move(undoAction)));
}

void DocumentController::newDocument(const QSize &size)
{
    const bool wasModified = isModified();
    const QSize normalized(
        std::clamp(
            size.width(),
            DocumentLimits::minimumCanvasEdge,
            DocumentLimits::maximumCanvasEdge),
        std::clamp(
            size.height(),
            DocumentLimits::minimumCanvasEdge,
            DocumentLimits::maximumCanvasEdge));
    m_document = Document::createDefault(normalized);
    m_undoStack.clear();
    m_undoStack.setClean();
    m_currentContentRevision = 0;
    m_savedContentRevision = 0;
    m_nextContentRevision = 0;
    emit documentReplaced();
    emit documentChanged();
    emit layerThumbnailsReset();
    emit activeLayerChanged(m_document.activeLayerId);
    if (wasModified) {
        emit modifiedChanged(false);
    }
}

void DocumentController::loadDocument(Document document)
{
    const bool wasModified = isModified();
    m_document = std::move(document);
    ensureActiveLayer();
    m_undoStack.clear();
    m_undoStack.setClean();
    m_currentContentRevision = 0;
    m_savedContentRevision = 0;
    m_nextContentRevision = 0;
    emit documentReplaced();
    emit documentChanged();
    emit layerThumbnailsReset();
    emit activeLayerChanged(m_document.activeLayerId);
    if (wasModified) {
        emit modifiedChanged(false);
    }
}

void DocumentController::loadRecoveredDocument(Document document)
{
    const bool wasModified = isModified();
    m_document = std::move(document);
    ensureActiveLayer();
    m_undoStack.clear();
    m_currentContentRevision = 1;
    m_savedContentRevision = 0;
    m_nextContentRevision = 1;
    emit documentReplaced();
    emit documentChanged();
    emit layerThumbnailsReset();
    emit activeLayerChanged(m_document.activeLayerId);
    if (!wasModified) {
        emit modifiedChanged(true);
    }
}

void DocumentController::markSaved()
{
    const bool wasModified = isModified();
    m_savedContentRevision = m_currentContentRevision;
    m_undoStack.setClean();
    if (wasModified) {
        emit modifiedChanged(false);
    }
}

bool DocumentController::resizeCanvas(const QSize &size)
{
    if (size == m_document.size
        || size.width() < DocumentLimits::minimumCanvasEdge
        || size.height() < DocumentLimits::minimumCanvasEdge
        || size.width() > DocumentLimits::maximumCanvasEdge
        || size.height() > DocumentLimits::maximumCanvasEdge) {
        return false;
    }

    const auto previous = std::make_shared<Document>(m_document);
    auto resized = std::make_shared<Document>(m_document);
    const qreal horizontalScale =
        static_cast<qreal>(size.width()) / m_document.size.width();
    const qreal verticalScale =
        static_cast<qreal>(size.height()) / m_document.size.height();
    const qreal widthScale = std::sqrt(horizontalScale * verticalScale);
    QTransform transform;
    transform.scale(horizontalScale, verticalScale);
    resized->size = size;
    QHash<qint64, QImage> transformedMasks;
    for (Layer &layer : resized->layers) {
        for (Stroke &stroke : layer.strokes) {
            for (StrokePoint &point : stroke.points) {
                const QPointF mapped = transform.map(point.position);
                point.position = QPointF(
                    std::clamp(
                        mapped.x(),
                        0.0,
                        static_cast<qreal>(size.width())),
                    std::clamp(
                        mapped.y(),
                        0.0,
                        static_cast<qreal>(size.height())));
            }
            stroke.width = std::clamp(
                stroke.width * widthScale,
                DocumentLimits::minimumStrokeWidth,
                DocumentLimits::maximumStrokeWidth);
            if (!transformMask(
                    stroke.clipMask,
                    size,
                    transform,
                    transformedMasks)) {
                return false;
            }
        }
    }
    if (distinctClipMaskBytes(*resized)
        > DocumentLimits::maximumDistinctClipMaskBytes) {
        return false;
    }

    const auto apply = [this](const std::shared_ptr<Document> &state) {
        const QSize oldSize = m_document.size;
        const QSize newSize = state->size;
        QTransform resizeTransform;
        resizeTransform.scale(
            static_cast<qreal>(newSize.width()) / oldSize.width(),
            static_cast<qreal>(newSize.height()) / oldSize.height());
        m_document = *state;
        emit canvasResized(oldSize, newSize, resizeTransform);
        notifyDocumentChanged();
        emit layerThumbnailsReset();
    };
    pushDocumentCommand(
        tr("Resize canvas"),
        [apply, resized]() { apply(resized); },
        [apply, previous]() { apply(previous); });
    return true;
}

void DocumentController::setActiveLayer(const QUuid &id)
{
    if (m_document.activeLayerId == id || !m_document.layer(id)) {
        return;
    }
    m_document.activeLayerId = id;
    emit activeLayerChanged(id);
}

void DocumentController::addStroke(const QUuid &layerId, Stroke stroke)
{
    Layer *layer = m_document.layer(layerId);
    if (!layer
        || layer->strokes.size() >= DocumentLimits::maximumStrokesPerLayer
        || totalStrokeCount(m_document)
            >= DocumentLimits::maximumTotalStrokes
        || stroke.id.isNull()
        || containsStrokeId(m_document, stroke.id)
        || (stroke.mode != StrokeMode::Paint
            && stroke.mode != StrokeMode::Erase
            && stroke.mode != StrokeMode::Fill)
        || !stroke.color.isValid()
        || !std::isfinite(stroke.width)
        || stroke.width < DocumentLimits::minimumStrokeWidth
        || stroke.width > DocumentLimits::maximumStrokeWidth
        || !isValidBrushSettings(stroke.brush)
        || stroke.points.isEmpty()
        || (!stroke.clipMask.isNull()
            && (stroke.clipMask.size() != m_document.size
                || stroke.clipMask.format() != QImage::Format_Grayscale8))) {
        return;
    }

    const qsizetype currentPointCount = totalPointCount(m_document);
    if (currentPointCount >= DocumentLimits::maximumTotalPoints) {
        return;
    }
    const qsizetype availablePoints =
        DocumentLimits::maximumTotalPoints - currentPointCount;
    const qsizetype acceptedPointCount = std::min(
        stroke.points.size(),
        std::min(
            static_cast<qsizetype>(DocumentLimits::maximumPointsPerStroke),
            availablePoints));
    stroke.points.resize(acceptedPointCount);
    if (!std::all_of(
            stroke.points.cbegin(),
            stroke.points.cend(),
            [this](const StrokePoint &point) {
                return isValidStrokePoint(point, m_document.size);
            })) {
        return;
    }
    if (!stroke.clipMask.isNull()) {
        const quint64 existingMaskBytes =
            distinctClipMaskBytes(m_document);
        bool alreadyPresent = false;
        for (const Layer &existingLayer : m_document.layers) {
            for (const Stroke &existingStroke : existingLayer.strokes) {
                if (!existingStroke.clipMask.isNull()
                    && (existingStroke.clipMask.cacheKey()
                            == stroke.clipMask.cacheKey()
                        || existingStroke.clipMask
                            == stroke.clipMask)) {
                    stroke.clipMask = existingStroke.clipMask;
                    alreadyPresent = true;
                    break;
                }
            }
            if (alreadyPresent) {
                break;
            }
        }
        if (!alreadyPresent
            && (existingMaskBytes
                    > DocumentLimits::maximumDistinctClipMaskBytes
                || static_cast<quint64>(
                    stroke.clipMask.sizeInBytes())
                    > DocumentLimits::maximumDistinctClipMaskBytes
                        - existingMaskBytes)) {
            return;
        }
    }

    const QUuid strokeId = stroke.id;
    auto redoAction = [
        this,
        layerId,
        stroke = std::move(stroke)
    ]() {
        if (Layer *target = m_document.layer(layerId)) {
            target->strokes.append(stroke);
            emit strokePresenceChanged(
                layerId,
                stroke.id,
                stroke.clipMask,
                true);
            notifyDocumentChanged();
            emit layerThumbnailChanged(layerId);
        }
    };
    auto undoAction = [this, layerId, strokeId]() {
        if (Layer *layer = m_document.layer(layerId)) {
            for (int index = layer->strokes.size() - 1; index >= 0; --index) {
                if (layer->strokes[index].id == strokeId) {
                    const QImage clipMask =
                        layer->strokes[index].clipMask;
                    layer->strokes.removeAt(index);
                    emit strokePresenceChanged(
                        layerId,
                        strokeId,
                        clipMask,
                        false);
                    notifyDocumentChanged();
                    emit layerThumbnailChanged(layerId);
                    return;
                }
            }
        }
    };
    pushDocumentCommand(
        tr("Draw stroke"),
        std::move(redoAction),
        std::move(undoAction));
}

bool DocumentController::moveStrokes(
    const QUuid &layerId,
    const QVector<QUuid> &strokeIds,
    const QPointF &delta,
    const QImage &selectionMask)
{
    if (!std::isfinite(delta.x())
        || !std::isfinite(delta.y())
        || (qFuzzyIsNull(delta.x()) && qFuzzyIsNull(delta.y()))) {
        return false;
    }
    QTransform transform;
    transform.translate(delta.x(), delta.y());
    return transformStrokes(
        layerId,
        strokeIds,
        transform,
        1.0,
        tr("Move selection"),
        selectionMask);
}

bool DocumentController::scaleStrokes(
    const QUuid &layerId,
    const QVector<QUuid> &strokeIds,
    const QPointF &center,
    qreal factor,
    const QImage &selectionMask)
{
    if (!std::isfinite(center.x())
        || !std::isfinite(center.y())
        || !std::isfinite(factor)
        || factor <= 0.0) {
        return false;
    }
    QTransform transform;
    transform.translate(center.x(), center.y());
    transform.scale(factor, factor);
    transform.translate(-center.x(), -center.y());
    return transformStrokes(
        layerId,
        strokeIds,
        transform,
        factor,
        tr("Scale selection"),
        selectionMask);
}

bool DocumentController::rotateStrokes(
    const QUuid &layerId,
    const QVector<QUuid> &strokeIds,
    const QPointF &center,
    qreal degrees,
    const QImage &selectionMask)
{
    if (!std::isfinite(center.x())
        || !std::isfinite(center.y())
        || !std::isfinite(degrees)
        || qFuzzyIsNull(degrees)) {
        return false;
    }
    QTransform transform;
    transform.translate(center.x(), center.y());
    transform.rotate(degrees);
    transform.translate(-center.x(), -center.y());
    return transformStrokes(
        layerId,
        strokeIds,
        transform,
        1.0,
        tr("Rotate selection"),
        selectionMask);
}

bool DocumentController::duplicateStrokes(
    const QUuid &layerId,
    const QVector<QUuid> &strokeIds,
    const QPointF &delta)
{
    const Layer *layer = m_document.layer(layerId);
    if (!layer
        || strokeIds.isEmpty()
        || !std::isfinite(delta.x())
        || !std::isfinite(delta.y())) {
        return false;
    }

    const QSet<QUuid> requested(strokeIds.cbegin(), strokeIds.cend());
    QVector<Stroke> copies;
    QVector<QUuid> sourceIds;
    QVector<QUuid> duplicateIds;
    qsizetype addedPoints = 0;
    QHash<qint64, QImage> transformedMasks;
    for (const Stroke &stroke : layer->strokes) {
        if (!requested.contains(stroke.id)) {
            continue;
        }
        Stroke copy = stroke;
        copy.id = QUuid::createUuid();
        for (StrokePoint &point : copy.points) {
            point.position += delta;
            if (!isValidStrokePoint(point, m_document.size)) {
                return false;
            }
        }
        if (!copy.clipMask.isNull()) {
            QTransform transform;
            transform.translate(delta.x(), delta.y());
            if (!transformMask(
                    copy.clipMask,
                    m_document.size,
                    transform,
                    transformedMasks)) {
                return false;
            }
        }
        addedPoints += copy.points.size();
        sourceIds.append(stroke.id);
        duplicateIds.append(copy.id);
        copies.append(std::move(copy));
    }
    if (copies.isEmpty()
        || layer->strokes.size()
            > DocumentLimits::maximumStrokesPerLayer - copies.size()
        || totalStrokeCount(m_document)
            > DocumentLimits::maximumTotalStrokes - copies.size()
        || totalPointCount(m_document)
            > DocumentLimits::maximumTotalPoints - addedPoints) {
        return false;
    }
    Document withCopies = m_document;
    if (Layer *target = withCopies.layer(layerId)) {
        target->strokes += copies;
    }
    if (distinctClipMaskBytes(withCopies)
        > DocumentLimits::maximumDistinctClipMaskBytes) {
        return false;
    }

    const QSet<QUuid> duplicateSet(
        duplicateIds.cbegin(),
        duplicateIds.cend());
    auto redoAction = [
        this,
        layerId,
        copies,
        sourceIds,
        duplicateIds,
        delta
    ]() {
        if (Layer *target = m_document.layer(layerId)) {
            target->strokes += copies;
            emit strokesDuplicated(
                layerId,
                sourceIds,
                duplicateIds,
                delta,
                true);
            notifyDocumentChanged();
            emit layerThumbnailChanged(layerId);
        }
    };
    auto undoAction = [
        this,
        layerId,
        sourceIds,
        duplicateIds,
        duplicateSet,
        delta
    ]() {
        if (Layer *target = m_document.layer(layerId)) {
            target->strokes.removeIf([&duplicateSet](const Stroke &stroke) {
                return duplicateSet.contains(stroke.id);
            });
            emit strokesDuplicated(
                layerId,
                sourceIds,
                duplicateIds,
                delta,
                false);
            notifyDocumentChanged();
            emit layerThumbnailChanged(layerId);
        }
    };
    pushDocumentCommand(
        tr("Duplicate selection"),
        std::move(redoAction),
        std::move(undoAction));
    return true;
}

bool DocumentController::transformStrokes(
    const QUuid &layerId,
    const QVector<QUuid> &strokeIds,
    const QTransform &transform,
    qreal widthScale,
    const QString &text,
    const QImage &selectionMask)
{
    const Layer *layer = m_document.layer(layerId);
    if (!layer
        || strokeIds.isEmpty()
        || !std::isfinite(widthScale)
        || widthScale <= 0.0) {
        return false;
    }

    const QSet<QUuid> requested(strokeIds.cbegin(), strokeIds.cend());
    if (!selectionMask.isNull()) {
        if (selectionMask.size() != m_document.size
            || selectionMask.format() != QImage::Format_Grayscale8) {
            return false;
        }

        QVector<Stroke> before = layer->strokes;
        QVector<Stroke> after;
        after.reserve(before.size() + requested.size());
        QVector<QUuid> transformedIds;
        for (const Stroke &stroke : before) {
            if (!requested.contains(stroke.id)) {
                after.append(stroke);
                continue;
            }

            const QImage selectedMask =
                maskedPart(stroke.clipMask, selectionMask, true);
            if (!maskHasContent(selectedMask)) {
                after.append(stroke);
                continue;
            }

            const QImage remainderMask =
                maskedPart(stroke.clipMask, selectionMask, false);
            if (maskHasContent(remainderMask)) {
                Stroke remainder = stroke;
                remainder.id = QUuid::createUuid();
                remainder.clipMask = remainderMask;
                after.append(std::move(remainder));
            }

            Stroke transformed = stroke;
            for (StrokePoint &point : transformed.points) {
                const QPointF mapped = transform.map(point.position);
                if (!std::isfinite(mapped.x())
                    || !std::isfinite(mapped.y())) {
                    return false;
                }
                point.position = QPointF(
                    std::clamp(
                        mapped.x(),
                        0.0,
                        static_cast<qreal>(m_document.size.width())),
                    std::clamp(
                        mapped.y(),
                        0.0,
                        static_cast<qreal>(m_document.size.height())));
            }
            transformed.width = std::clamp(
                transformed.width * widthScale,
                DocumentLimits::minimumStrokeWidth,
                DocumentLimits::maximumStrokeWidth);
            transformed.clipMask = transformedMask(
                selectedMask,
                m_document.size,
                transform);
            if (transformed.clipMask.isNull()) {
                return false;
            }
            transformedIds.append(transformed.id);
            after.append(std::move(transformed));
        }

        if (transformedIds.isEmpty()
            || after.size() > DocumentLimits::maximumStrokesPerLayer) {
            return false;
        }
        Document transformedDocument = m_document;
        if (Layer *target = transformedDocument.layer(layerId)) {
            target->strokes = after;
        }
        if (totalStrokeCount(transformedDocument)
                > DocumentLimits::maximumTotalStrokes
            || totalPointCount(transformedDocument)
                > DocumentLimits::maximumTotalPoints
            || distinctClipMaskBytes(transformedDocument)
                > DocumentLimits::maximumDistinctClipMaskBytes) {
            return false;
        }

        const auto replace = [
            this,
            layerId,
            transformedIds
        ](const QVector<Stroke> &strokes, const QTransform &appliedTransform) {
            if (Layer *target = m_document.layer(layerId)) {
                target->strokes = strokes;
                emit strokesTransformed(
                    layerId,
                    transformedIds,
                    appliedTransform);
                notifyDocumentChanged();
                emit layerThumbnailChanged(layerId);
            }
        };
        bool invertible = false;
        const QTransform inverse = transform.inverted(&invertible);
        if (!invertible) {
            return false;
        }
        pushDocumentCommand(
            text,
            [replace, after, transform]() { replace(after, transform); },
            [replace, before, inverse]() { replace(before, inverse); });
        return true;
    }

    QVector<Stroke> before;
    QVector<Stroke> after;
    QVector<QUuid> transformedIds;
    QHash<qint64, QImage> transformedMasks;
    for (const Stroke &stroke : layer->strokes) {
        if (!requested.contains(stroke.id)) {
            continue;
        }
        Stroke transformed = stroke;
        for (StrokePoint &point : transformed.points) {
            point.position = transform.map(point.position);
            if (!isValidStrokePoint(point, m_document.size)) {
                return false;
            }
        }
        transformed.width = std::clamp(
            transformed.width * widthScale,
            DocumentLimits::minimumStrokeWidth,
            DocumentLimits::maximumStrokeWidth);
        if (!transformMask(
                transformed.clipMask,
                m_document.size,
                transform,
                transformedMasks)) {
            return false;
        }
        before.append(stroke);
        after.append(std::move(transformed));
        transformedIds.append(stroke.id);
    }
    if (after.isEmpty()) {
        return false;
    }
    Document transformedDocument = m_document;
    if (Layer *target = transformedDocument.layer(layerId)) {
        QHash<QUuid, Stroke> replacements;
        for (const Stroke &stroke : after) {
            replacements.insert(stroke.id, stroke);
        }
        for (Stroke &stroke : target->strokes) {
            const auto replacement = replacements.constFind(stroke.id);
            if (replacement != replacements.cend()) {
                stroke = replacement.value();
            }
        }
    }
    if (distinctClipMaskBytes(transformedDocument)
        > DocumentLimits::maximumDistinctClipMaskBytes) {
        return false;
    }

    const auto replace = [
        this,
        layerId,
        transformedIds
    ](const QVector<Stroke> &strokes, const QTransform &appliedTransform) {
        if (Layer *target = m_document.layer(layerId)) {
            QHash<QUuid, Stroke> replacements;
            for (const Stroke &stroke : strokes) {
                replacements.insert(stroke.id, stroke);
            }
            for (Stroke &stroke : target->strokes) {
                const auto replacement = replacements.constFind(stroke.id);
                if (replacement != replacements.cend()) {
                    stroke = replacement.value();
                }
            }
            emit strokesTransformed(
                layerId,
                transformedIds,
                appliedTransform);
            notifyDocumentChanged();
            emit layerThumbnailChanged(layerId);
        }
    };
    bool invertible = false;
    const QTransform inverse = transform.inverted(&invertible);
    if (!invertible) {
        return false;
    }
    pushDocumentCommand(
        text,
        [replace, after, transform]() { replace(after, transform); },
        [replace, before, inverse]() { replace(before, inverse); });
    return true;
}

void DocumentController::removeStrokes(
    const QUuid &layerId,
    const QVector<QUuid> &strokeIds)
{
    const Layer *layer = m_document.layer(layerId);
    if (!layer || strokeIds.isEmpty()) {
        return;
    }

    const QSet<QUuid> requested(strokeIds.cbegin(), strokeIds.cend());
    QVector<QPair<int, Stroke>> removed;
    for (int index = 0; index < layer->strokes.size(); ++index) {
        if (requested.contains(layer->strokes[index].id)) {
            removed.append({index, layer->strokes[index]});
        }
    }
    if (removed.isEmpty()) {
        return;
    }

    auto redoAction = [this, layerId, requested]() {
        if (Layer *target = m_document.layer(layerId)) {
            target->strokes.removeIf([&requested](const Stroke &stroke) {
                return requested.contains(stroke.id);
            });
            notifyDocumentChanged();
            emit layerThumbnailChanged(layerId);
        }
    };
    auto undoAction = [this, layerId, removed]() {
        if (Layer *target = m_document.layer(layerId)) {
            for (const auto &entry : removed) {
                target->strokes.insert(
                    std::clamp(
                        entry.first,
                        0,
                        static_cast<int>(target->strokes.size())),
                    entry.second);
            }
            notifyDocumentChanged();
            emit layerThumbnailChanged(layerId);
        }
    };
    pushDocumentCommand(
        tr("Delete selection"),
        std::move(redoAction),
        std::move(undoAction));
}

void DocumentController::addLayer()
{
    if (m_document.layers.size() >= DocumentLimits::maximumLayers) {
        return;
    }
    Layer layer;
    layer.name = nextLayerName();
    const int insertionIndex = m_document.layers.size();
    const QUuid previousActive = m_document.activeLayerId;
    const QUuid layerId = layer.id;
    auto redoAction = [this, layer, insertionIndex, layerId]() {
        const int index = std::clamp(
            insertionIndex,
            0,
            static_cast<int>(m_document.layers.size()));
        m_document.layers.insert(index, layer);
        m_document.activeLayerId = layerId;
        notifyDocumentChanged();
        emit layerThumbnailChanged(layerId);
        emit activeLayerChanged(layerId);
    };
    auto undoAction = [this, layerId, previousActive]() {
        const int index = m_document.layerIndex(layerId);
        if (index >= 0) {
            m_document.layers.removeAt(index);
        }
        m_document.activeLayerId = previousActive;
        ensureActiveLayer();
        notifyDocumentChanged();
        emit layerThumbnailChanged(layerId);
        emit activeLayerChanged(m_document.activeLayerId);
    };
    pushDocumentCommand(
        tr("Add layer"),
        std::move(redoAction),
        std::move(undoAction));
}

void DocumentController::duplicateLayer(const QUuid &id)
{
    const int sourceIndex = m_document.layerIndex(id);
    if (sourceIndex < 0
        || m_document.layers.size() >= DocumentLimits::maximumLayers) {
        return;
    }
    const qsizetype sourcePointCount =
        layerPointCount(m_document.layers[sourceIndex]);
    const qsizetype existingPointCount = totalPointCount(m_document);
    const qsizetype sourceStrokeCount =
        m_document.layers[sourceIndex].strokes.size();
    const qsizetype existingStrokeCount = totalStrokeCount(m_document);
    if (sourcePointCount > DocumentLimits::maximumTotalPoints
        || existingPointCount > DocumentLimits::maximumTotalPoints
        || sourcePointCount
            > DocumentLimits::maximumTotalPoints - existingPointCount
        || sourceStrokeCount > DocumentLimits::maximumTotalStrokes
        || existingStrokeCount > DocumentLimits::maximumTotalStrokes
        || sourceStrokeCount
            > DocumentLimits::maximumTotalStrokes - existingStrokeCount) {
        return;
    }
    Layer copy = m_document.layers[sourceIndex];
    copy.id = QUuid::createUuid();
    copy.name = tr("%1 copy").arg(copy.name);
    if (copy.name.size() > DocumentLimits::maximumLayerNameLength) {
        copy.name.truncate(DocumentLimits::maximumLayerNameLength);
    }
    for (Stroke &stroke : copy.strokes) {
        stroke.id = QUuid::createUuid();
    }
    const int insertionIndex = sourceIndex + 1;
    const QUuid copyId = copy.id;
    const QUuid previousActive = m_document.activeLayerId;
    auto redoAction = [this, copy, insertionIndex, copyId]() {
        m_document.layers.insert(
            std::clamp(
                insertionIndex,
                0,
                static_cast<int>(m_document.layers.size())),
            copy);
        m_document.activeLayerId = copyId;
        notifyDocumentChanged();
        emit layerThumbnailChanged(copyId);
        emit activeLayerChanged(copyId);
    };
    auto undoAction = [this, copyId, previousActive]() {
        const int index = m_document.layerIndex(copyId);
        if (index >= 0) {
            m_document.layers.removeAt(index);
        }
        m_document.activeLayerId = previousActive;
        ensureActiveLayer();
        notifyDocumentChanged();
        emit layerThumbnailChanged(copyId);
        emit activeLayerChanged(m_document.activeLayerId);
    };
    pushDocumentCommand(
        tr("Duplicate layer"),
        std::move(redoAction),
        std::move(undoAction));
}

void DocumentController::removeLayer(const QUuid &id)
{
    const int index = m_document.layerIndex(id);
    if (index < 0 || m_document.layers.size() <= 1) {
        return;
    }
    const Layer removedLayer = m_document.layers[index];
    const QUuid previousActive = m_document.activeLayerId;
    const int nextIndex = index > 0 ? index - 1 : 1;
    const QUuid nextActive = m_document.layers[nextIndex].id;
    auto redoAction = [this, id, nextActive]() {
        const int currentIndex = m_document.layerIndex(id);
        if (currentIndex >= 0) {
            m_document.layers.removeAt(currentIndex);
        }
        if (m_document.activeLayerId == id) {
            m_document.activeLayerId = nextActive;
        }
        ensureActiveLayer();
        notifyDocumentChanged();
        emit layerThumbnailChanged(id);
        emit activeLayerChanged(m_document.activeLayerId);
    };
    auto undoAction = [this, removedLayer, index, previousActive]() {
        m_document.layers.insert(
            std::clamp(
                index,
                0,
                static_cast<int>(m_document.layers.size())),
            removedLayer);
        m_document.activeLayerId = previousActive;
        ensureActiveLayer();
        notifyDocumentChanged();
        emit layerThumbnailChanged(removedLayer.id);
        emit activeLayerChanged(m_document.activeLayerId);
    };
    pushDocumentCommand(
        tr("Delete layer"),
        std::move(redoAction),
        std::move(undoAction));
}

void DocumentController::clearLayer(const QUuid &id)
{
    Layer *layer = m_document.layer(id);
    if (!layer || layer->strokes.isEmpty()) {
        return;
    }
    const QVector<Stroke> previousStrokes = layer->strokes;
    auto redoAction = [this, id]() {
        if (Layer *target = m_document.layer(id)) {
            target->strokes.clear();
            notifyDocumentChanged();
            emit layerThumbnailChanged(id);
        }
    };
    auto undoAction = [this, id, previousStrokes]() {
        if (Layer *target = m_document.layer(id)) {
            target->strokes = previousStrokes;
            notifyDocumentChanged();
            emit layerThumbnailChanged(id);
        }
    };
    pushDocumentCommand(
        tr("Clear layer"),
        std::move(redoAction),
        std::move(undoAction));
}

void DocumentController::renameLayer(const QUuid &id, const QString &name)
{
    Layer *layer = m_document.layer(id);
    const QString normalized = name.trimmed();
    if (!layer
        || normalized.isEmpty()
        || normalized.size() > DocumentLimits::maximumLayerNameLength
        || layer->name == normalized) {
        return;
    }
    const QString previousName = layer->name;
    auto redoAction = [this, id, normalized]() {
        if (Layer *target = m_document.layer(id)) {
            target->name = normalized;
            notifyDocumentChanged();
        }
    };
    auto undoAction = [this, id, previousName]() {
        if (Layer *target = m_document.layer(id)) {
            target->name = previousName;
            notifyDocumentChanged();
        }
    };
    pushDocumentCommand(
        tr("Rename layer"),
        std::move(redoAction),
        std::move(undoAction));
}

void DocumentController::setLayerVisible(const QUuid &id, bool visible)
{
    Layer *layer = m_document.layer(id);
    if (!layer || layer->visible == visible) {
        return;
    }
    const bool previous = layer->visible;
    auto redoAction = [this, id, visible]() {
        if (Layer *target = m_document.layer(id)) {
            target->visible = visible;
            notifyDocumentChanged();
        }
    };
    auto undoAction = [this, id, previous]() {
        if (Layer *target = m_document.layer(id)) {
            target->visible = previous;
            notifyDocumentChanged();
        }
    };
    pushDocumentCommand(
        tr("Toggle layer visibility"),
        std::move(redoAction),
        std::move(undoAction));
}

void DocumentController::setLayerOpacity(const QUuid &id, qreal opacity)
{
    Layer *layer = m_document.layer(id);
    if (!std::isfinite(opacity)) {
        return;
    }
    const qreal normalized = std::clamp(opacity, 0.0, 1.0);
    if (!layer || qFuzzyCompare(layer->opacity, normalized)) {
        return;
    }
    const qreal previous = layer->opacity;
    auto redoAction = [this, id, normalized]() {
        if (Layer *target = m_document.layer(id)) {
            target->opacity = normalized;
            notifyDocumentChanged();
        }
    };
    auto undoAction = [this, id, previous]() {
        if (Layer *target = m_document.layer(id)) {
            target->opacity = previous;
            notifyDocumentChanged();
        }
    };
    pushDocumentCommand(
        tr("Change layer opacity"),
        std::move(redoAction),
        std::move(undoAction),
        layerOpacityMergeId,
        id);
}

void DocumentController::moveLayer(const QUuid &id, int offset)
{
    const int from = m_document.layerIndex(id);
    const int to = from + offset;
    if (from < 0 || to < 0 || to >= m_document.layers.size() || from == to) {
        return;
    }
    auto move = [this](int source, int destination) {
        m_document.layers.move(source, destination);
        notifyDocumentChanged();
    };
    pushDocumentCommand(
        tr("Move layer"),
        [move, from, to]() { move(from, to); },
        [move, from, to]() { move(to, from); });
}

void DocumentController::setWobbleAmount(qreal amount)
{
    if (!std::isfinite(amount)) {
        return;
    }
    const qreal normalized = std::clamp(
        amount,
        DocumentLimits::minimumWobbleAmount,
        DocumentLimits::maximumWobbleAmount);
    if (qFuzzyCompare(m_document.wobbleAmount, normalized)) {
        return;
    }
    const qreal previous = m_document.wobbleAmount;
    auto apply = [this](qreal value) {
        m_document.wobbleAmount = value;
        notifyDocumentChanged();
    };
    pushDocumentCommand(
        tr("Change wobble"),
        [apply, normalized]() { apply(normalized); },
        [apply, previous]() { apply(previous); },
        wobbleAmountMergeId);
}

void DocumentController::setAnimationFrames(int frames)
{
    const int normalized = std::clamp(
        frames,
        DocumentLimits::minimumAnimationFrames,
        DocumentLimits::maximumAnimationFrames);
    if (m_document.animationFrames == normalized) {
        return;
    }
    const int previous = m_document.animationFrames;
    auto apply = [this](int value) {
        m_document.animationFrames = value;
        notifyDocumentChanged();
    };
    pushDocumentCommand(
        tr("Change animation frames"),
        [apply, normalized]() { apply(normalized); },
        [apply, previous]() { apply(previous); },
        animationFramesMergeId);
}

void DocumentController::setFramesPerSecond(qreal fps)
{
    if (!std::isfinite(fps)) {
        return;
    }
    const qreal normalized = std::clamp(
        fps,
        DocumentLimits::minimumFramesPerSecond,
        DocumentLimits::maximumFramesPerSecond);
    if (qFuzzyCompare(m_document.framesPerSecond, normalized)) {
        return;
    }
    const qreal previous = m_document.framesPerSecond;
    auto apply = [this](qreal value) {
        m_document.framesPerSecond = value;
        notifyDocumentChanged();
    };
    pushDocumentCommand(
        tr("Change animation speed"),
        [apply, normalized]() { apply(normalized); },
        [apply, previous]() { apply(previous); },
        framesPerSecondMergeId);
}

void DocumentController::pushDocumentCommand(
    QString text,
    std::function<void()> redoAction,
    std::function<void()> undoAction,
    int mergeId,
    const QUuid &mergeScope)
{
    const quint64 previousRevision = m_currentContentRevision;
    const quint64 nextRevision = ++m_nextContentRevision;
    auto trackedRedo = [
        this,
        redoAction = std::move(redoAction),
        nextRevision
    ]() {
        redoAction();
        setContentRevision(nextRevision);
    };
    auto trackedUndo = [
        this,
        undoAction = std::move(undoAction),
        previousRevision
    ]() {
        undoAction();
        setContentRevision(previousRevision);
    };
    m_undoStack.push(new LambdaCommand(
        std::move(text),
        std::move(trackedRedo),
        std::move(trackedUndo),
        mergeId,
        mergeScope));
}

void DocumentController::setContentRevision(quint64 revision)
{
    const bool wasModified = isModified();
    m_currentContentRevision = revision;
    const bool modified = isModified();
    if (modified != wasModified) {
        emit modifiedChanged(modified);
    }
}

void DocumentController::notifyDocumentChanged()
{
    emit documentChanged();
}

void DocumentController::ensureActiveLayer()
{
    if (m_document.layer(m_document.activeLayerId)) {
        return;
    }
    if (m_document.layers.isEmpty()) {
        Layer layer;
        layer.name = QStringLiteral("Layer 1");
        m_document.layers.append(layer);
    }
    m_document.activeLayerId = m_document.layers.constLast().id;
}

QString DocumentController::nextLayerName() const
{
    int number = m_document.layers.size() + 1;
    while (true) {
        const QString candidate = tr("Layer %1").arg(number);
        bool exists = false;
        for (const Layer &layer : m_document.layers) {
            if (layer.name == candidate) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            return candidate;
        }
        ++number;
    }
}

}
