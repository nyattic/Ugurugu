#include "io/DocumentSerializer.hpp"

#include "document/DocumentLimits.hpp"
#include "document/DocumentOperations.hpp"
#include "document/LayerHierarchy.hpp"
#include "document/SelectionOperation.hpp"
#include "io/serializer/DocumentJsonCodec.hpp"
#include "io/serializer/MaskAssetTable.hpp"
#include "io/serializer/SerializerSchema.hpp"

#include <QCryptographicHash>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSaveFile>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <list>
#include <memory>
#include <utility>

namespace wobble
{

struct DocumentSerializer::SerializationCache::Impl
{
    struct Payload
    {
        QByteArray compressed;
        std::list<QString>::iterator lruPosition;
    };

    explicit Impl(qint64 requestedCapacity)
        : capacity(std::clamp(requestedCapacity,
              0LL,
              DocumentSerializer::SerializationCache::maximumPayloadBytes))
    {
    }

    QByteArray payload(const QString &key)
    {
        const auto found = payloads.find(key);
        if (found == payloads.end())
        {
            ++statistics.payloadCacheMisses;
            return {};
        }
        lru.splice(lru.begin(), lru, found->lruPosition);
        ++statistics.payloadCacheHits;
        return found->compressed;
    }

    void storePayload(const QString &key, const QByteArray &compressed)
    {
        if (compressed.isEmpty() || compressed.size() > capacity)
        {
            return;
        }
        auto existing = payloads.find(key);
        if (existing != payloads.end())
        {
            residentBytes -= existing->compressed.size();
            lru.erase(existing->lruPosition);
            payloads.erase(existing);
        }
        while (!lru.empty() && residentBytes > capacity - compressed.size())
        {
            const QString evictedKey = lru.back();
            const auto evicted = payloads.find(evictedKey);
            if (evicted != payloads.end())
            {
                residentBytes -= evicted->compressed.size();
                payloads.erase(evicted);
            }
            lru.pop_back();
        }
        lru.push_front(key);
        Payload payload{compressed, lru.begin()};
        residentBytes += compressed.size();
        payloads.insert(key, std::move(payload));
    }

    void clear()
    {
        payloads.clear();
        lru.clear();
        residentBytes = 0;
    }

    qint64 capacity = 0;
    qint64 residentBytes = 0;
    QHash<QString, Payload> payloads;
    std::list<QString> lru;
    DocumentSerializer::SerializationCache::Stats statistics;
};

struct DocumentSerializer::PreparedDocument::Impl
{
    Document document;
    serializer_detail::PreparedPlan plan;
};

struct DocumentSerializer::ImmutableBackingLease::Impl
{
    serializer_detail::ImmutableBackings backings;
};

namespace
{

using namespace serializer_detail;

struct DocumentValidationStats
{
    qsizetype totalStrokeCount = 0;
    qsizetype totalPointCount = 0;
    quint64 distinctMaskBytes = 0;
};

bool addSerializedBytes(qint64 &total, qint64 amount, qint64 maximumBytes)
{
    if (amount < 0 || amount > maximumBytes || total > maximumBytes - amount)
    {
        return false;
    }
    total += amount;
    return true;
}

bool sameImageIdentity(const QImage &left, const QImage &right)
{
    return left.isNull() == right.isNull()
           && (left.isNull()
               || (left.cacheKey() == right.cacheKey()
                   && left.size() == right.size()
                   && left.format() == right.format()));
}

bool samePackedIdentity(const QByteArray &left, const QByteArray &right)
{
    return left.size() == right.size()
           && (left.isEmpty() || left.constData() == right.constData());
}

bool samePixelSelectionIdentity(const std::optional<PixelSelectionOp> &left,
    const std::optional<PixelSelectionOp> &right)
{
    if (left.has_value() != right.has_value())
    {
        return false;
    }
    if (!left)
    {
        return true;
    }
    return left->canvasSize == right->canvasSize
           && left->sourceBounds == right->sourceBounds
           && samePackedIdentity(left->packedMask, right->packedMask)
           && left->transform == right->transform
           && left->sampling == right->sampling
           && left->clearSource == right->clearSource
           && left->drawDestination == right->drawDestination;
}

bool sameStrokeIdentity(const Stroke &left, const Stroke &right)
{
    const bool samePoints =
        left.points.size() == right.points.size()
        && (left.points.isEmpty()
            || left.points.constData() == right.points.constData());
    return left.id == right.id && left.seed == right.seed
           && left.mode == right.mode && left.color == right.color
           && left.width == right.width && left.brush == right.brush
           && samePoints && left.visibilityClip == right.visibilityClip
           && sameImageIdentity(left.clipMask, right.clipMask)
           && sameImageIdentity(left.fillMask, right.fillMask)
           && samePixelSelectionIdentity(
               left.pixelSelectionOp, right.pixelSelectionOp)
           && left.reframeOp == right.reframeOp;
}

QString backingIdentity(const void *data, qsizetype size)
{
    if (!data || size <= 0)
    {
        return {};
    }
    return QStringLiteral("%1:%2")
        .arg(reinterpret_cast<quintptr>(data), 0, 16)
        .arg(size);
}

void rememberImmutableImage(ImmutableBackings &backings, const QImage &image)
{
    if (!image.isNull())
    {
        backings.images.insert(image.cacheKey(), image);
    }
}

void rememberImmutableBytes(
    ImmutableBackings &backings, const QByteArray &bytes)
{
    const QString identity = backingIdentity(bytes.constData(), bytes.size());
    if (!identity.isEmpty())
    {
        backings.byteArrays.insert(identity, bytes);
    }
}

void rememberImmutablePoints(
    ImmutableBackings &backings, const QVector<StrokePoint> &points)
{
    const QString identity = backingIdentity(points.constData(), points.size());
    if (!identity.isEmpty())
    {
        backings.pointVectors.insert(identity, points);
    }
}

ImmutableBackings immutableBackingsFromPlan(const PreparedPlan *base)
{
    ImmutableBackings backings;
    if (!base)
    {
        return backings;
    }
    for (const ClipAssetMeta &asset : base->clipAssets)
    {
        rememberImmutableImage(backings, asset.image);
    }
    for (const BinaryAssetMeta &asset : base->binaryAssets)
    {
        rememberImmutableBytes(backings, asset.region.packedMask);
    }
    for (const StrokeMeta &stroke : base->strokes)
    {
        rememberImmutablePoints(backings, stroke.snapshot.points);
        rememberImmutableImage(backings, stroke.snapshot.clipMask);
        rememberImmutableImage(backings, stroke.snapshot.fillMask);
        if (stroke.snapshot.pixelSelectionOp)
        {
            rememberImmutableBytes(
                backings, stroke.snapshot.pixelSelectionOp->packedMask);
        }
    }
    return backings;
}

class DocumentFreezer final
{
public:
    explicit DocumentFreezer(
        const PreparedPlan *base, const ImmutableBackings *trusted = nullptr)
        : m_reusable(immutableBackingsFromPlan(base))
    {
        if (!trusted)
        {
            return;
        }
        for (auto entry = trusted->images.cbegin();
            entry != trusted->images.cend();
            ++entry)
        {
            m_reusable.images.insert(entry.key(), entry.value());
        }
        for (auto entry = trusted->byteArrays.cbegin();
            entry != trusted->byteArrays.cend();
            ++entry)
        {
            m_reusable.byteArrays.insert(entry.key(), entry.value());
        }
        for (auto entry = trusted->pointVectors.cbegin();
            entry != trusted->pointVectors.cend();
            ++entry)
        {
            m_reusable.pointVectors.insert(entry.key(), entry.value());
        }
    }

    Document freeze(const Document &source)
    {
        Document frozen;
        frozen.size = source.size;
        frozen.background = source.background;
        frozen.animationFrames = source.animationFrames;
        frozen.framesPerSecond = source.framesPerSecond;
        frozen.wobbleAmount = source.wobbleAmount;
        frozen.activeLayerId = source.activeLayerId;
        frozen.layers.reserve(source.layers.size());
        for (const Layer &layer : source.layers)
        {
            frozen.layers.append(freezeLayer(layer));
        }
        return frozen;
    }

private:
    static QString owningCopy(const QString &source)
    {
        if (source.isNull())
        {
            return {};
        }
        return QString(source.constData(), source.size());
    }

    QImage freezeImage(const QImage &source)
    {
        if (source.isNull())
        {
            return {};
        }
        const qint64 identity = source.cacheKey();
        const auto reusable = m_reusable.images.constFind(identity);
        if (reusable != m_reusable.images.cend()
            && sameImageIdentity(source, reusable.value()))
        {
            return reusable.value();
        }
        const auto alreadyFrozen = m_frozen.images.constFind(identity);
        if (alreadyFrozen != m_frozen.images.cend()
            && alreadyFrozen->size() == source.size()
            && alreadyFrozen->format() == source.format())
        {
            return alreadyFrozen.value();
        }
        QImage frozen = source.copy();
        m_frozen.images.insert(identity, frozen);
        return frozen;
    }

    QByteArray freezeBytes(const QByteArray &source)
    {
        if (source.isEmpty())
        {
            return source;
        }
        const QString identity =
            backingIdentity(source.constData(), source.size());
        const auto reusable = m_reusable.byteArrays.constFind(identity);
        if (reusable != m_reusable.byteArrays.cend()
            && samePackedIdentity(source, reusable.value()))
        {
            return reusable.value();
        }
        const auto alreadyFrozen = m_frozen.byteArrays.constFind(identity);
        if (alreadyFrozen != m_frozen.byteArrays.cend())
        {
            return alreadyFrozen.value();
        }
        const QByteArray frozen(source.constData(), source.size());
        m_frozen.byteArrays.insert(identity, frozen);
        return frozen;
    }

    QVector<StrokePoint> freezePoints(const QVector<StrokePoint> &source)
    {
        if (source.isEmpty())
        {
            return {};
        }
        const QString identity =
            backingIdentity(source.constData(), source.size());
        const auto reusable = m_reusable.pointVectors.constFind(identity);
        if (reusable != m_reusable.pointVectors.cend()
            && reusable->size() == source.size())
        {
            return reusable.value();
        }
        const auto alreadyFrozen = m_frozen.pointVectors.constFind(identity);
        if (alreadyFrozen != m_frozen.pointVectors.cend())
        {
            return alreadyFrozen.value();
        }
        QVector<StrokePoint> frozen;
        frozen.reserve(source.size());
        for (const StrokePoint &point : source)
        {
            frozen.append(point);
        }
        m_frozen.pointVectors.insert(identity, frozen);
        return frozen;
    }

    Stroke freezeStroke(const Stroke &source)
    {
        Stroke frozen;
        frozen.id = source.id;
        frozen.seed = source.seed;
        frozen.mode = source.mode;
        frozen.color = source.color;
        frozen.width = source.width;
        frozen.brush = source.brush;
        frozen.points = freezePoints(source.points);
        frozen.visibilityClip = source.visibilityClip;
        frozen.clipMask = freezeImage(source.clipMask);
        frozen.fillMask = freezeImage(source.fillMask);
        if (source.pixelSelectionOp)
        {
            PixelSelectionOp operation = *source.pixelSelectionOp;
            operation.packedMask = freezeBytes(operation.packedMask);
            frozen.pixelSelectionOp = std::move(operation);
        }
        else
        {
            frozen.pixelSelectionOp.reset();
        }
        frozen.reframeOp = source.reframeOp;
        return frozen;
    }

    Layer freezeLayer(const Layer &source)
    {
        Layer frozen;
        frozen.id = source.id;
        frozen.name = owningCopy(source.name);
        frozen.kind = source.kind;
        frozen.parentGroupId = source.parentGroupId;
        frozen.clipToLayerBelow = source.clipToLayerBelow;
        frozen.visible = source.visible;
        frozen.reference = source.reference;
        frozen.opacity = source.opacity;
        frozen.blendMode = source.blendMode;
        frozen.initialCanvasSize = source.initialCanvasSize;
        frozen.strokes.reserve(source.strokes.size());
        for (const Stroke &stroke : source.strokes)
        {
            frozen.strokes.append(freezeStroke(stroke));
        }
        return frozen;
    }

    ImmutableBackings m_reusable;
    ImmutableBackings m_frozen;
};

enum class MetadataReuseStatus
{
    NotApplicable,
    Invalid,
    TooLarge,
    Success
};

struct MetadataReuseResult
{
    MetadataReuseStatus status = MetadataReuseStatus::NotApplicable;
    Document document;
    PreparedPlan plan;
};

bool sameStrokeVectorBacking(
    const QVector<Stroke> &left, const QVector<Stroke> &right)
{
    // A non-const QVector access detaches before exposing writable storage.
    // Equality here therefore proves that the candidate still references the
    // immutable backing owned by baseDocument. Never broaden this to an
    // element-wise comparison: retained writable aliases must take the full
    // freezer/validator path.
    return left.size() == right.size()
           && (left.isEmpty() || left.constData() == right.constData());
}

QString owningStringCopy(const QString &source)
{
    if (source.isNull())
    {
        return {};
    }
    return QString(source.constData(), source.size());
}

MetadataReuseResult reusePreparedContentForMetadataEdit(const Document &source,
    const Document &baseDocument,
    const PreparedPlan &basePlan,
    qint64 maximumBytes,
    QString *error)
{
    MetadataReuseResult result;
    if (source.size != baseDocument.size
        || source.layers.size() != baseDocument.layers.size())
    {
        return result;
    }

    QHash<QUuid, const Layer *> baseLayers;
    baseLayers.reserve(baseDocument.layers.size());
    for (const Layer &layer : baseDocument.layers)
    {
        baseLayers.insert(layer.id, &layer);
    }
    QSet<QUuid> matchedLayers;
    matchedLayers.reserve(source.layers.size());
    for (const Layer &layer : source.layers)
    {
        const auto base = baseLayers.constFind(layer.id);
        if (base == baseLayers.cend() || matchedLayers.contains(layer.id)
            || layer.initialCanvasSize != (*base)->initialCanvasSize
            || !sameStrokeVectorBacking(layer.strokes, (*base)->strokes))
        {
            return result;
        }
        matchedLayers.insert(layer.id);
    }

    if (!source.background.isValid())
    {
        setError(
            error, DocumentSerializer::tr("The canvas background is invalid."));
        result.status = MetadataReuseStatus::Invalid;
        return result;
    }
    if (source.animationFrames < DocumentLimits::minimumAnimationFrames
        || source.animationFrames > DocumentLimits::maximumAnimationFrames
        || !std::isfinite(source.framesPerSecond)
        || source.framesPerSecond < DocumentLimits::minimumFramesPerSecond
        || source.framesPerSecond > DocumentLimits::maximumFramesPerSecond
        || !std::isfinite(source.wobbleAmount)
        || source.wobbleAmount < DocumentLimits::minimumWobbleAmount
        || source.wobbleAmount > DocumentLimits::maximumWobbleAmount)
    {
        setError(error,
            DocumentSerializer::tr("The animation settings are invalid."));
        result.status = MetadataReuseStatus::Invalid;
        return result;
    }

    bool activeLayerFound = false;
    for (const Layer &layer : source.layers)
    {
        activeLayerFound = activeLayerFound
                           || (layer.id == source.activeLayerId
                               && layer.kind == LayerKind::Paint);
        if (layer.name.trimmed().isEmpty()
            || layer.name.size() > DocumentLimits::maximumLayerNameLength
            || !std::isfinite(layer.opacity) || layer.opacity < 0.0
            || layer.opacity > 1.0 || !isValidLayerBlendMode(layer.blendMode)
            || !isValidLayerKind(layer.kind)
            || (layer.kind == LayerKind::Group
                && (!layer.strokes.isEmpty() || layer.clipToLayerBelow
                    || layer.reference)))
        {
            setError(error,
                DocumentSerializer::tr("A layer contains invalid data."));
            result.status = MetadataReuseStatus::Invalid;
            return result;
        }
    }
    if (!analyzeLayerHierarchy(source).isValid())
    {
        setError(
            error, DocumentSerializer::tr("The layer hierarchy is invalid."));
        result.status = MetadataReuseStatus::Invalid;
        return result;
    }
    const bool validActiveLayer =
        !std::any_of(source.layers.cbegin(),
            source.layers.cend(),
            [](const Layer &layer)
            {
                return layer.kind == LayerKind::Paint;
            })
            ? source.activeLayerId.isNull()
            : !source.activeLayerId.isNull() && activeLayerFound;
    if (!validActiveLayer)
    {
        setError(
            error, DocumentSerializer::tr("The active layer ID is invalid."));
        result.status = MetadataReuseStatus::Invalid;
        return result;
    }

    qint64 previousMetadataBytes = 0;
    qint64 nextMetadataBytes = 0;
    const auto addMetadataSize = [](qint64 &total, qint64 bytes)
    {
        return addSerializedBytes(
            total, bytes, std::numeric_limits<qint64>::max());
    };
    const qint64 previousRootBytes =
        QJsonDocument(rootToJson(baseDocument, {}, {}, {}))
            .toJson(QJsonDocument::Compact)
            .size();
    const qint64 nextRootBytes = QJsonDocument(rootToJson(source, {}, {}, {}))
                                     .toJson(QJsonDocument::Compact)
                                     .size();
    if (!addMetadataSize(previousMetadataBytes, previousRootBytes)
        || !addMetadataSize(nextMetadataBytes, nextRootBytes))
    {
        setError(
            error, DocumentSerializer::tr("The project is too large to save."));
        result.status = MetadataReuseStatus::TooLarge;
        return result;
    }
    for (const Layer &layer : source.layers)
    {
        const Layer &baseLayer = **baseLayers.constFind(layer.id);
        const qint64 previousLayerBytes =
            QJsonDocument(layerSkeletonToJson(baseLayer))
                .toJson(QJsonDocument::Compact)
                .size();
        const qint64 nextLayerBytes = QJsonDocument(layerSkeletonToJson(layer))
                                          .toJson(QJsonDocument::Compact)
                                          .size();
        if (!addMetadataSize(previousMetadataBytes, previousLayerBytes)
            || !addMetadataSize(nextMetadataBytes, nextLayerBytes))
        {
            setError(error,
                DocumentSerializer::tr("The project is too large to save."));
            result.status = MetadataReuseStatus::TooLarge;
            return result;
        }
    }
    qint64 compactSize = basePlan.compactSize;
    // Layer and asset topology is byte-for-byte unchanged. Replacing only
    // the root/layer skeleton contribution keeps the prepared size exact
    // without rebuilding the per-stroke plan.
    if (nextMetadataBytes >= previousMetadataBytes)
    {
        const qint64 increase = nextMetadataBytes - previousMetadataBytes;
        if (increase > maximumBytes || compactSize > maximumBytes - increase)
        {
            setError(error,
                DocumentSerializer::tr("The project is too large to save."));
            result.status = MetadataReuseStatus::TooLarge;
            return result;
        }
        compactSize += increase;
    }
    else
    {
        const qint64 decrease = previousMetadataBytes - nextMetadataBytes;
        if (compactSize < decrease)
        {
            setError(error,
                DocumentSerializer::tr("The project contains invalid "
                                       "operations or too much mask data."));
            result.status = MetadataReuseStatus::Invalid;
            return result;
        }
        compactSize -= decrease;
        if (compactSize > maximumBytes)
        {
            setError(error,
                DocumentSerializer::tr("The project is too large to save."));
            result.status = MetadataReuseStatus::TooLarge;
            return result;
        }
    }

    Document frozen;
    frozen.size = source.size;
    frozen.background = source.background;
    frozen.animationFrames = source.animationFrames;
    frozen.framesPerSecond = source.framesPerSecond;
    frozen.wobbleAmount = source.wobbleAmount;
    frozen.activeLayerId = source.activeLayerId;
    if (source.layers.constData() == baseDocument.layers.constData())
    {
        frozen.layers = baseDocument.layers;
    }
    else
    {
        frozen.layers.reserve(source.layers.size());
        for (const Layer &layer : source.layers)
        {
            const Layer &baseLayer = **baseLayers.constFind(layer.id);
            Layer frozenLayer;
            frozenLayer.id = layer.id;
            frozenLayer.name = owningStringCopy(layer.name);
            frozenLayer.kind = layer.kind;
            frozenLayer.parentGroupId = layer.parentGroupId;
            frozenLayer.clipToLayerBelow = layer.clipToLayerBelow;
            frozenLayer.visible = layer.visible;
            frozenLayer.reference = layer.reference;
            frozenLayer.opacity = layer.opacity;
            frozenLayer.blendMode = layer.blendMode;
            frozenLayer.initialCanvasSize = layer.initialCanvasSize;
            frozenLayer.strokes = baseLayer.strokes;
            frozen.layers.append(std::move(frozenLayer));
        }
    }

    result.document = std::move(frozen);
    result.plan = basePlan;
    result.plan.compactSize = compactSize;
    result.status = MetadataReuseStatus::Success;
    return result;
}

bool isValidIncrementalStroke(const Stroke &stroke, const QSize &canvasSize)
{
    const QRect canvasRect(QPoint(), canvasSize);
    if ((stroke.mode != StrokeMode::Paint && stroke.mode != StrokeMode::Erase
            && stroke.mode != StrokeMode::Fill)
        || stroke.id.isNull() || !stroke.color.isValid()
        || !std::isfinite(stroke.width)
        || stroke.width < DocumentLimits::minimumStrokeWidth
        || stroke.width > DocumentLimits::maximumStrokeWidth
        || !isValidBrushSettings(stroke.brush) || stroke.points.isEmpty()
        || stroke.points.size() > DocumentLimits::maximumPointsPerStroke
        || stroke.pixelSelectionOp || stroke.reframeOp
        || (stroke.visibilityClip
            && (stroke.visibilityClip->isEmpty()
                || !canvasRect.contains(*stroke.visibilityClip)))
        || (!stroke.clipMask.isNull()
            && (stroke.clipMask.size() != canvasSize
                || stroke.clipMask.format() != QImage::Format_Grayscale8))
        || (!stroke.fillMask.isNull()
            && (stroke.mode != StrokeMode::Fill
                || stroke.fillMask.size() != canvasSize
                || stroke.fillMask.format() != QImage::Format_Grayscale8)))
    {
        return false;
    }
    return std::all_of(stroke.points.cbegin(),
        stroke.points.cend(),
        [](const StrokePoint &point)
        {
            return std::isfinite(point.position.x())
                   && std::isfinite(point.position.y())
                   && std::abs(point.position.x())
                          <= DocumentLimits::maximumStoredCoordinateMagnitude
                   && std::abs(point.position.y())
                          <= DocumentLimits::maximumStoredCoordinateMagnitude
                   && std::isfinite(point.pressure) && point.pressure >= 0.0
                   && point.pressure <= 1.0;
        });
}

QVector<StrokePoint> freezeIncrementalPoints(const QVector<StrokePoint> &source)
{
    QVector<StrokePoint> frozen;
    frozen.reserve(source.size());
    for (const StrokePoint &point : source)
    {
        frozen.append(point);
    }
    return frozen;
}

template <typename Cache>
bool freezeIncrementalClipMask(const QImage &source,
    PreparedPlan &plan,
    Cache &cache,
    qint64 maximumBytes,
    qint64 &serializedGrowth,
    QImage &frozen,
    DocumentSerializer::AppendStrokeStatus &status)
{
    if (source.isNull())
    {
        frozen = {};
        return true;
    }
    const auto knownId = plan.clipIdsByIdentity.constFind(source.cacheKey());
    if (knownId != plan.clipIdsByIdentity.cend())
    {
        const auto asset = plan.clipAssets.constFind(knownId.value());
        if (asset == plan.clipAssets.cend()
            || asset->image.size() != source.size()
            || asset->image.format() != source.format())
        {
            status = DocumentSerializer::AppendStrokeStatus::Invalid;
            return false;
        }
        frozen = asset->image;
        plan.clipIdsByIdentity.insert(frozen.cacheKey(), knownId.value());
        return true;
    }

    const QByteArray bytes = canonicalMaskBytes(source);
    if (bytes.isEmpty())
    {
        status = DocumentSerializer::AppendStrokeStatus::Invalid;
        return false;
    }
    ++cache.statistics.clipMaskContentHashes;
    const QString id = maskContentId(source.width(), source.height(), bytes);
    const auto existing = plan.clipAssets.constFind(id);
    if (existing != plan.clipAssets.cend())
    {
        if (existing->image.size() != source.size()
            || existing->image != source)
        {
            status = DocumentSerializer::AppendStrokeStatus::Invalid;
            return false;
        }
        frozen = existing->image;
        plan.clipIdsByIdentity.insert(frozen.cacheKey(), id);
        return true;
    }

    const quint64 maskBytes = source.sizeInBytes();
    if (maskBytes
        > DocumentLimits::maximumDistinctClipMaskBytes - plan.distinctMaskBytes)
    {
        status = DocumentSerializer::AppendStrokeStatus::MaskLimit;
        return false;
    }
    const QByteArray compressed = compressedPayload(cache, false, id, bytes);
    frozen = source.copy();
    if (compressed.isEmpty() || frozen.isNull())
    {
        status = DocumentSerializer::AppendStrokeStatus::Invalid;
        return false;
    }
    const qint64 entryBytes =
        serializedClipMaskSize(id, frozen, compressed.size());
    if (entryBytes < 0)
    {
        status = DocumentSerializer::AppendStrokeStatus::TooLarge;
        return false;
    }
    qint64 assetGrowth = entryBytes;
    if (!plan.clipAssets.isEmpty())
    {
        ++assetGrowth;
    }
    if (!addSerializedBytes(serializedGrowth, assetGrowth, maximumBytes))
    {
        status = DocumentSerializer::AppendStrokeStatus::TooLarge;
        return false;
    }
    plan.clipAssets.insert(
        id, ClipAssetMeta{id, frozen, entryBytes, compressed.size()});
    plan.clipIdsByIdentity.insert(frozen.cacheKey(), id);
    plan.distinctMaskBytes += maskBytes;
    return true;
}

Stroke freezeIncrementalStroke(const Stroke &source)
{
    Stroke frozen;
    frozen.id = source.id;
    frozen.seed = source.seed;
    frozen.mode = source.mode;
    frozen.color = source.color;
    frozen.width = source.width;
    frozen.brush = source.brush;
    frozen.points = freezeIncrementalPoints(source.points);
    frozen.visibilityClip = source.visibilityClip;
    return frozen;
}

QString clipMaskId(const QImage &mask, const ClipMaskTable &table)
{
    if (mask.isNull())
    {
        return {};
    }
    return table.idByCacheKey.value(mask.cacheKey());
}

QString binaryMaskId(const Stroke &stroke, const BinaryMaskTable &table)
{
    if (!stroke.pixelSelectionOp)
    {
        return {};
    }
    return table.idByIdentity.value(
        binaryMaskIdentity(pixelSelectionMaskRegion(*stroke.pixelSelectionOp)));
}

template <typename Cache>
bool serializedSizeWithinLimit(const Document &document,
    const ClipMaskTable &clipMasks,
    const BinaryMaskTable &binaryMasks,
    const PreparedPlan *base,
    PreparedPlan &plan,
    Cache &cache,
    qint64 maximumBytes)
{
    for (auto entry = clipMasks.entries.cbegin();
        entry != clipMasks.entries.cend();
        ++entry)
    {
        plan.clipAssets.insert(entry.key(),
            ClipAssetMeta{entry->id,
                entry->image,
                entry->serializedBytes,
                entry->compressedBytes});
    }
    plan.clipIdsByIdentity = clipMasks.idByCacheKey;
    for (auto entry = binaryMasks.entries.cbegin();
        entry != binaryMasks.entries.cend();
        ++entry)
    {
        plan.binaryAssets.insert(entry.key(),
            BinaryAssetMeta{entry->id,
                entry->region,
                entry->serializedBytes,
                entry->compressedBytes});
    }
    plan.binaryIdsByIdentity = binaryMasks.idByIdentity;

    qint64 serializedLayerBytes = 0;
    for (const Layer &layer : document.layers)
    {
        qint64 serializedStrokeBytes = 0;
        for (const Stroke &stroke : layer.strokes)
        {
            const QString resolvedClipId =
                clipMaskId(stroke.clipMask, clipMasks);
            const QString resolvedFillId =
                clipMaskId(stroke.fillMask, clipMasks);
            const QString resolvedBinaryId = binaryMaskId(stroke, binaryMasks);
            qint64 bytes = -1;
            if (base)
            {
                const auto cached = base->strokes.constFind(stroke.id);
                if (cached != base->strokes.cend()
                    && cached->clipMaskId == resolvedClipId
                    && cached->fillMaskId == resolvedFillId
                    && cached->binaryMaskId == resolvedBinaryId
                    && sameStrokeIdentity(stroke, cached->snapshot))
                {
                    bytes = cached->serializedBytes;
                }
            }
            if (bytes < 0)
            {
                ++cache.statistics.strokeSerializations;
                bytes =
                    QJsonDocument(strokeToJson(stroke, clipMasks, binaryMasks))
                        .toJson(QJsonDocument::Compact)
                        .size();
            }
            plan.strokes.insert(stroke.id,
                StrokeMeta{stroke,
                    resolvedClipId,
                    resolvedFillId,
                    resolvedBinaryId,
                    bytes});
            if (!addSerializedBytes(serializedStrokeBytes, bytes, maximumBytes))
            {
                return false;
            }
        }
        const qint64 strokeArrayBytes =
            2 + serializedStrokeBytes
            + std::max<qint64>(0, layer.strokes.size() - 1);
        const qint64 skeletonBytes = QJsonDocument(layerSkeletonToJson(layer))
                                         .toJson(QJsonDocument::Compact)
                                         .size();
        const qint64 layerBytes = skeletonBytes - 2 + strokeArrayBytes;
        if (!addSerializedBytes(serializedLayerBytes, layerBytes, maximumBytes))
        {
            return false;
        }
    }

    const qint64 layerArrayBytes =
        2 + serializedLayerBytes
        + std::max<qint64>(0, document.layers.size() - 1);
    const qint64 maskArrayBytes =
        2 + clipMasks.serializedEntryBytes
        + std::max<qint64>(0, clipMasks.entries.size() - 1);
    const qint64 binaryMaskArrayBytes =
        2 + binaryMasks.serializedEntryBytes
        + std::max<qint64>(0, binaryMasks.entries.size() - 1);
    const qint64 rootSkeletonBytes =
        QJsonDocument(rootToJson(document, {}, {}, {}))
            .toJson(QJsonDocument::Compact)
            .size();
    qint64 total = rootSkeletonBytes - 6;
    const bool withinLimit =
        addSerializedBytes(total, layerArrayBytes, maximumBytes)
        && addSerializedBytes(total, maskArrayBytes, maximumBytes)
        && addSerializedBytes(total, binaryMaskArrayBytes, maximumBytes);
    if (withinLimit)
    {
        plan.compactSize = total;
    }
    return withinLimit;
}

template <typename Cache>
bool tablesMatchPlan(const ClipMaskTable &clipMasks,
    const BinaryMaskTable &binaryMasks,
    const PreparedPlan &plan,
    Cache &)
{
    if (clipMasks.entries.size() != plan.clipAssets.size()
        || binaryMasks.entries.size() != plan.binaryAssets.size()
        || clipMasks.idByCacheKey != plan.clipIdsByIdentity
        || binaryMasks.idByIdentity != plan.binaryIdsByIdentity)
    {
        return false;
    }
    for (auto entry = clipMasks.entries.cbegin();
        entry != clipMasks.entries.cend();
        ++entry)
    {
        const auto expected = plan.clipAssets.constFind(entry.key());
        if (expected == plan.clipAssets.cend() || entry->id != expected->id
            || !sameImageIdentity(entry->image, expected->image)
            || entry->serializedBytes != expected->serializedEntryBytes
            || entry->compressedBytes != expected->compressedBytes
            || entry->compressedData.isEmpty())
        {
            return false;
        }
    }
    for (auto entry = binaryMasks.entries.cbegin();
        entry != binaryMasks.entries.cend();
        ++entry)
    {
        const auto expected = plan.binaryAssets.constFind(entry.key());
        if (expected == plan.binaryAssets.cend() || entry->id != expected->id
            || entry->region != expected->region
            || entry->serializedBytes != expected->serializedEntryBytes
            || entry->compressedBytes != expected->compressedBytes
            || entry->compressedData.isEmpty())
        {
            return false;
        }
    }
    return true;
}

template <typename Cache>
std::optional<QByteArray> serializePreparedDocument(const Document &document,
    const PreparedPlan &plan,
    Cache &cache,
    const QJsonObject &additionalRootFields = {})
{
    if (plan.compactSize <= 0
        || plan.compactSize > DocumentLimits::maximumProjectBytes)
    {
        return std::nullopt;
    }
    qint64 expectedSize = plan.compactSize;
    if (!additionalRootFields.isEmpty())
    {
        const qint64 baseRootSize =
            QJsonDocument(rootToJson(document, {}, {}, {}))
                .toJson(QJsonDocument::Compact)
                .size();
        const qint64 extendedRootSize = QJsonDocument(
            rootToJson(document, {}, {}, {}, additionalRootFields))
                                            .toJson(QJsonDocument::Compact)
                                            .size();
        const qint64 additionalBytes = extendedRootSize - baseRootSize;
        if (additionalBytes < 0
            || expectedSize
                   > DocumentLimits::maximumProjectBytes - additionalBytes)
        {
            return std::nullopt;
        }
        expectedSize += additionalBytes;
    }
    const ClipMaskTable clipMasks =
        buildClipMaskTable(document, &plan, cache, plan.compactSize, true);
    const BinaryMaskTable binaryMasks =
        buildBinaryMaskTable(document, &plan, cache, plan.compactSize, true);
    if (clipMasks.invalid || clipMasks.tooLarge || binaryMasks.invalid
        || binaryMasks.tooLarge
        || !tablesMatchPlan(clipMasks, binaryMasks, plan, cache))
    {
        return std::nullopt;
    }
    QJsonArray layers;
    for (const Layer &layer : document.layers)
    {
        layers.append(layerToJson(layer, clipMasks, binaryMasks));
    }
    QJsonArray masks;
    for (const SerializedClipMask &entry : clipMasks.entries)
    {
        masks.append(serializedClipMaskToJson(entry));
    }
    QJsonArray packedMasks;
    for (const SerializedBinaryMask &entry : binaryMasks.entries)
    {
        packedMasks.append(serializedBinaryMaskToJson(entry));
    }
    QByteArray data = QJsonDocument(
        rootToJson(document, layers, masks, packedMasks, additionalRootFields))
                          .toJson(QJsonDocument::Compact);
    if (data.size() != expectedSize)
    {
        return std::nullopt;
    }
    if (data.size() > DocumentLimits::maximumProjectBytes)
    {
        return std::nullopt;
    }
    return data;
}

bool validateCollectionBudgets(const QJsonArray &layers, QString *error)
{
    qsizetype remainingStrokes = DocumentLimits::maximumTotalStrokes;
    qsizetype remainingPoints = DocumentLimits::maximumTotalPoints;

    for (const QJsonValue &layerValue : layers)
    {
        if (!layerValue.isObject())
        {
            continue;
        }
        const QJsonValue strokesValue =
            layerValue.toObject().value(QStringLiteral("strokes"));
        if (!strokesValue.isArray())
        {
            continue;
        }
        const QJsonArray strokes = strokesValue.toArray();
        if (strokes.size() > DocumentLimits::maximumStrokesPerLayer
            || strokes.size() > remainingStrokes)
        {
            setError(error,
                DocumentSerializer::tr(
                    "The project contains too many strokes."));
            return false;
        }
        remainingStrokes -= strokes.size();

        for (const QJsonValue &strokeValue : strokes)
        {
            if (!strokeValue.isObject())
            {
                continue;
            }
            const QJsonValue pointsValue =
                strokeValue.toObject().value(QStringLiteral("points"));
            if (!pointsValue.isArray())
            {
                continue;
            }
            const qsizetype pointCount = pointsValue.toArray().size();
            if (pointCount > DocumentLimits::maximumPointsPerStroke
                || pointCount > remainingPoints)
            {
                setError(error,
                    DocumentSerializer::tr(
                        "The project contains too many points."));
                return false;
            }
            remainingPoints -= pointCount;
        }
    }
    return true;
}

bool validateDocument(const Document &document,
    int fileSchemaVersion,
    QString *error,
    DocumentValidationStats *stats = nullptr)
{
    if (document.size.width() < DocumentLimits::minimumCanvasEdge
        || document.size.height() < DocumentLimits::minimumCanvasEdge
        || document.size.width() > DocumentLimits::maximumCanvasEdge
        || document.size.height() > DocumentLimits::maximumCanvasEdge)
    {
        setError(error, DocumentSerializer::tr("The canvas size is invalid."));
        return false;
    }
    if (!document.background.isValid())
    {
        setError(
            error, DocumentSerializer::tr("The canvas background is invalid."));
        return false;
    }
    if (document.animationFrames < DocumentLimits::minimumAnimationFrames
        || document.animationFrames > DocumentLimits::maximumAnimationFrames
        || !std::isfinite(document.framesPerSecond)
        || document.framesPerSecond < DocumentLimits::minimumFramesPerSecond
        || document.framesPerSecond > DocumentLimits::maximumFramesPerSecond
        || !std::isfinite(document.wobbleAmount)
        || document.wobbleAmount < DocumentLimits::minimumWobbleAmount
        || document.wobbleAmount > DocumentLimits::maximumWobbleAmount)
    {
        setError(error,
            DocumentSerializer::tr("The animation settings are invalid."));
        return false;
    }
    if (document.layers.size() > DocumentLimits::maximumLayers)
    {
        setError(error, DocumentSerializer::tr("The layer count is invalid."));
        return false;
    }

    QSet<QUuid> layerIds;
    QSet<QUuid> strokeIds;
    QSet<qint64> clipMaskKeys;
    QSet<quintptr> packedMaskKeys;
    qsizetype totalStrokes = 0;
    qsizetype totalPoints = 0;
    quint64 clipMaskBytes = 0;
    bool activeLayerFound = false;
    for (const Layer &layer : document.layers)
    {
        if (layer.id.isNull() || layerIds.contains(layer.id))
        {
            setError(error,
                DocumentSerializer::tr(
                    "The project contains invalid layer IDs."));
            return false;
        }
        layerIds.insert(layer.id);
        activeLayerFound = activeLayerFound
                           || (layer.id == document.activeLayerId
                               && layer.kind == LayerKind::Paint);
        if (layer.name.trimmed().isEmpty()
            || layer.name.size() > DocumentLimits::maximumLayerNameLength
            || !std::isfinite(layer.opacity) || layer.opacity < 0.0
            || layer.opacity > 1.0 || !isValidLayerBlendMode(layer.blendMode)
            || !isValidLayerKind(layer.kind)
            || (layer.kind == LayerKind::Group
                && (!layer.strokes.isEmpty() || layer.clipToLayerBelow
                    || layer.reference))
            || layer.strokes.size() > DocumentLimits::maximumStrokesPerLayer
            || layer.strokes.size()
                   > DocumentLimits::maximumTotalStrokes - totalStrokes)
        {
            setError(error,
                DocumentSerializer::tr("A layer contains invalid data."));
            return false;
        }
        totalStrokes += layer.strokes.size();

        const auto validCanvasSize = [](const QSize &size)
        {
            return size.width() >= DocumentLimits::minimumCanvasEdge
                   && size.height() >= DocumentLimits::minimumCanvasEdge
                   && size.width() <= DocumentLimits::maximumCanvasEdge
                   && size.height() <= DocumentLimits::maximumCanvasEdge;
        };
        if (!validCanvasSize(layer.initialCanvasSize))
        {
            setError(error,
                DocumentSerializer::tr(
                    "A layer has an invalid initial canvas size."));
            return false;
        }
        QSize epochSize = layer.initialCanvasSize;
        for (const Stroke &stroke : layer.strokes)
        {
            if (stroke.id.isNull() || strokeIds.contains(stroke.id))
            {
                setError(error,
                    DocumentSerializer::tr(
                        "The project contains invalid stroke IDs."));
                return false;
            }
            strokeIds.insert(stroke.id);
            const auto validateMaskBudget =
                [&clipMaskKeys, &clipMaskBytes, error](const QImage &mask)
            {
                if (mask.isNull() || clipMaskKeys.contains(mask.cacheKey()))
                {
                    return true;
                }
                const quint64 maskBytes = mask.sizeInBytes();
                if (maskBytes > DocumentLimits::maximumDistinctClipMaskBytes
                                    - clipMaskBytes)
                {
                    setError(error,
                        DocumentSerializer::tr(
                            "The project contains too much mask data."));
                    return false;
                }
                clipMaskKeys.insert(mask.cacheKey());
                clipMaskBytes += maskBytes;
                return true;
            };
            if (!validateMaskBudget(stroke.clipMask)
                || !validateMaskBudget(stroke.fillMask))
            {
                return false;
            }
            const auto validatePackedBudget =
                [&packedMaskKeys, &clipMaskBytes, error](
                    const QByteArray &packed)
            {
                const quintptr backing =
                    reinterpret_cast<quintptr>(packed.constData());
                if (packedMaskKeys.contains(backing))
                {
                    return true;
                }
                const quint64 bytes = static_cast<quint64>(packed.size());
                if (bytes > DocumentLimits::maximumDistinctClipMaskBytes
                                - clipMaskBytes)
                {
                    setError(error,
                        DocumentSerializer::tr(
                            "The project contains too much binary mask data."));
                    return false;
                }
                packedMaskKeys.insert(backing);
                clipMaskBytes += bytes;
                return true;
            };
            if (stroke.pixelSelectionOp
                && !validatePackedBudget(stroke.pixelSelectionOp->packedMask))
            {
                return false;
            }

            const bool validCommonFields =
                stroke.color.isValid() && std::isfinite(stroke.width)
                && stroke.width >= DocumentLimits::minimumStrokeWidth
                && stroke.width <= DocumentLimits::maximumStrokeWidth
                && isValidBrushSettings(stroke.brush);
            if (!validCommonFields)
            {
                setError(error,
                    DocumentSerializer::tr("A stroke contains invalid data."));
                return false;
            }

            if (stroke.mode == StrokeMode::PixelSelection)
            {
                if (fileSchemaVersion < 6 || !stroke.pixelSelectionOp
                    || stroke.reframeOp || !stroke.points.isEmpty()
                    || stroke.visibilityClip || !stroke.clipMask.isNull()
                    || !stroke.fillMask.isNull()
                    || !isValidPixelSelectionOp(*stroke.pixelSelectionOp)
                    || stroke.pixelSelectionOp->canvasSize != epochSize)
                {
                    setError(error,
                        DocumentSerializer::tr(
                            "A pixel selection operation is invalid."));
                    return false;
                }
                continue;
            }
            if (stroke.mode == StrokeMode::Reframe)
            {
                if (fileSchemaVersion < 6 || !stroke.reframeOp
                    || stroke.pixelSelectionOp || !stroke.points.isEmpty()
                    || stroke.visibilityClip || !stroke.clipMask.isNull()
                    || !stroke.fillMask.isNull()
                    || !isValidReframeOp(*stroke.reframeOp)
                    || stroke.reframeOp->sourceSize != epochSize)
                {
                    setError(error,
                        DocumentSerializer::tr(
                            "A reframe operation is invalid."));
                    return false;
                }
                epochSize = stroke.reframeOp->targetSize;
                continue;
            }

            const QRect canvasRect(QPoint(), epochSize);
            const bool invalidVisibilityClip =
                stroke.visibilityClip
                && (stroke.visibilityClip->isEmpty()
                    || !canvasRect.contains(*stroke.visibilityClip));
            const bool invalidFillMask =
                (!stroke.fillMask.isNull()
                    && (stroke.mode != StrokeMode::Fill
                        || stroke.fillMask.size() != epochSize
                        || stroke.fillMask.format()
                               != QImage::Format_Grayscale8));
            if ((stroke.mode != StrokeMode::Paint
                    && stroke.mode != StrokeMode::Erase
                    && stroke.mode != StrokeMode::Fill)
                || stroke.pixelSelectionOp || stroke.reframeOp
                || stroke.points.isEmpty()
                || stroke.points.size() > DocumentLimits::maximumPointsPerStroke
                || (!stroke.clipMask.isNull()
                    && (stroke.clipMask.size() != epochSize
                        || stroke.clipMask.format()
                               != QImage::Format_Grayscale8))
                || invalidVisibilityClip || invalidFillMask
                || stroke.points.size()
                       > DocumentLimits::maximumTotalPoints - totalPoints)
            {
                setError(error,
                    DocumentSerializer::tr("A stroke contains invalid data."));
                return false;
            }
            totalPoints += stroke.points.size();
            for (const StrokePoint &point : stroke.points)
            {
                const bool outsideLegacyCanvas =
                    fileSchemaVersion <= 4
                    && (point.position.x() < 0.0 || point.position.y() < 0.0
                        || point.position.x() > epochSize.width()
                        || point.position.y() > epochSize.height());
                if (!std::isfinite(point.position.x())
                    || !std::isfinite(point.position.y())
                    || !std::isfinite(point.pressure)
                    || std::abs(point.position.x())
                           > DocumentLimits::maximumStoredCoordinateMagnitude
                    || std::abs(point.position.y())
                           > DocumentLimits::maximumStoredCoordinateMagnitude
                    || outsideLegacyCanvas || point.pressure < 0.0
                    || point.pressure > 1.0)
                {
                    setError(error,
                        DocumentSerializer::tr(
                            "A stroke contains an invalid point."));
                    return false;
                }
            }
        }
        if (epochSize != document.size)
        {
            setError(error,
                DocumentSerializer::tr(
                    "A layer does not end at the document canvas size."));
            return false;
        }
    }
    if (!analyzeLayerHierarchy(document).isValid())
    {
        setError(
            error, DocumentSerializer::tr("The layer hierarchy is invalid."));
        return false;
    }
    const bool validActiveLayer =
        !std::any_of(document.layers.cbegin(),
            document.layers.cend(),
            [](const Layer &layer)
            {
                return layer.kind == LayerKind::Paint;
            })
            ? document.activeLayerId.isNull()
            : !document.activeLayerId.isNull() && activeLayerFound;
    if (!validActiveLayer)
    {
        setError(
            error, DocumentSerializer::tr("The active layer ID is invalid."));
        return false;
    }
    if (stats)
    {
        stats->totalStrokeCount = totalStrokes;
        stats->totalPointCount = totalPoints;
        stats->distinctMaskBytes = clipMaskBytes;
    }
    return true;
}

void normalizeLayerInitialCanvasSizes(Document &document)
{
    for (Layer &layer : document.layers)
    {
        if (layer.initialCanvasSize.isValid())
        {
            continue;
        }
        layer.initialCanvasSize = document.size;
        for (const Stroke &operation : layer.strokes)
        {
            if (operation.reframeOp)
            {
                layer.initialCanvasSize = operation.reframeOp->sourceSize;
                break;
            }
            if (operation.pixelSelectionOp)
            {
                layer.initialCanvasSize =
                    operation.pixelSelectionOp->canvasSize;
                break;
            }
        }
    }
}

}

DocumentSerializer::SerializationCache::SerializationCache(
    qint64 payloadCapacityBytes)
    : m_impl(std::make_unique<Impl>(payloadCapacityBytes))
{
}

DocumentSerializer::SerializationCache::~SerializationCache() = default;

DocumentSerializer::SerializationCache::SerializationCache(
    SerializationCache &&) noexcept = default;

DocumentSerializer::SerializationCache &
DocumentSerializer::SerializationCache::operator=(
    SerializationCache &&) noexcept = default;

void DocumentSerializer::SerializationCache::clear()
{
    if (m_impl)
    {
        m_impl->clear();
    }
}

void DocumentSerializer::SerializationCache::resetStats()
{
    if (m_impl)
    {
        m_impl->statistics = {};
    }
}

DocumentSerializer::SerializationCache::Stats
DocumentSerializer::SerializationCache::stats() const
{
    return m_impl ? m_impl->statistics : Stats{};
}

qint64 DocumentSerializer::SerializationCache::payloadBytes() const
{
    return m_impl ? m_impl->residentBytes : 0;
}

qint64 DocumentSerializer::SerializationCache::payloadCapacityBytes() const
{
    return m_impl ? m_impl->capacity : 0;
}

DocumentSerializer::PreparedDocument::PreparedDocument() = default;
DocumentSerializer::PreparedDocument::~PreparedDocument() = default;
DocumentSerializer::PreparedDocument::PreparedDocument(
    const PreparedDocument &) = default;
DocumentSerializer::PreparedDocument &
DocumentSerializer::PreparedDocument::operator=(
    const PreparedDocument &) = default;
DocumentSerializer::PreparedDocument::PreparedDocument(
    PreparedDocument &&) noexcept = default;
DocumentSerializer::PreparedDocument &
DocumentSerializer::PreparedDocument::operator=(
    PreparedDocument &&) noexcept = default;

DocumentSerializer::PreparedDocument::PreparedDocument(
    std::shared_ptr<const Impl> impl)
    : m_impl(std::move(impl))
{
}

bool DocumentSerializer::PreparedDocument::isValid() const
{
    return static_cast<bool>(m_impl);
}

const Document &DocumentSerializer::PreparedDocument::document() const
{
    static const Document empty;
    return m_impl ? m_impl->document : empty;
}

qint64 DocumentSerializer::PreparedDocument::compactSize() const
{
    return m_impl ? m_impl->plan.compactSize : 0;
}

qsizetype DocumentSerializer::PreparedDocument::totalStrokeCount() const
{
    return m_impl ? m_impl->plan.totalStrokeCount : 0;
}

qsizetype DocumentSerializer::PreparedDocument::totalPointCount() const
{
    return m_impl ? m_impl->plan.totalPointCount : 0;
}

DocumentSerializer::ImmutableBackingLease::ImmutableBackingLease() = default;
DocumentSerializer::ImmutableBackingLease::~ImmutableBackingLease() = default;
DocumentSerializer::ImmutableBackingLease::ImmutableBackingLease(
    const ImmutableBackingLease &) = default;
DocumentSerializer::ImmutableBackingLease &
DocumentSerializer::ImmutableBackingLease::operator=(
    const ImmutableBackingLease &) = default;
DocumentSerializer::ImmutableBackingLease::ImmutableBackingLease(
    ImmutableBackingLease &&) noexcept = default;
DocumentSerializer::ImmutableBackingLease &
DocumentSerializer::ImmutableBackingLease::operator=(
    ImmutableBackingLease &&) noexcept = default;

DocumentSerializer::ImmutableBackingLease::ImmutableBackingLease(
    std::shared_ptr<const Impl> impl)
    : m_impl(std::move(impl))
{
}

bool DocumentSerializer::ImmutableBackingLease::isValid() const
{
    return static_cast<bool>(m_impl);
}

std::optional<DocumentSerializer::PreparedDocument> DocumentSerializer::prepare(
    Document document, SerializationCache &cache, QString *error)
{
    return prepare(std::move(document),
        cache,
        nullptr,
        DocumentLimits::maximumProjectBytes,
        error);
}

std::optional<DocumentSerializer::PreparedDocument> DocumentSerializer::prepare(
    Document document,
    SerializationCache &cache,
    const PreparedDocument *base,
    qint64 maximumBytes,
    QString *error)
{
    return prepare(
        std::move(document), cache, base, nullptr, maximumBytes, error);
}

DocumentSerializer::ImmutableBackingLease
DocumentSerializer::retainImmutableBackings(
    const PreparedDocument &source, const QVector<Stroke> &strokes)
{
    if (!source.m_impl)
    {
        return {};
    }
    auto impl = std::make_shared<ImmutableBackingLease::Impl>();
    for (const Stroke &stroke : strokes)
    {
        const auto trusted = source.m_impl->plan.strokes.constFind(stroke.id);
        if (trusted == source.m_impl->plan.strokes.cend()
            || !sameStrokeIdentity(stroke, trusted->snapshot))
        {
            return {};
        }
        rememberImmutablePoints(impl->backings, trusted->snapshot.points);
        rememberImmutableImage(impl->backings, trusted->snapshot.clipMask);
        rememberImmutableImage(impl->backings, trusted->snapshot.fillMask);
        if (trusted->snapshot.pixelSelectionOp)
        {
            rememberImmutableBytes(
                impl->backings, trusted->snapshot.pixelSelectionOp->packedMask);
        }
    }
    return ImmutableBackingLease(std::move(impl));
}

std::optional<DocumentSerializer::PreparedDocument> DocumentSerializer::prepare(
    Document document,
    SerializationCache &cache,
    const PreparedDocument *base,
    const ImmutableBackingLease *trusted,
    qint64 maximumBytes,
    QString *error)
{
    if (!cache.m_impl || maximumBytes < 0
        || maximumBytes > DocumentLimits::maximumProjectBytes
        || (base && !base->m_impl) || (trusted && !trusted->m_impl))
    {
        setError(error,
            DocumentSerializer::tr("The project contains invalid "
                                   "operations or too much mask data."));
        return std::nullopt;
    }

    if (base)
    {
        MetadataReuseResult reused =
            reusePreparedContentForMetadataEdit(document,
                base->m_impl->document,
                base->m_impl->plan,
                maximumBytes,
                error);
        if (reused.status == MetadataReuseStatus::Success)
        {
            auto impl = std::make_shared<PreparedDocument::Impl>();
            impl->document = std::move(reused.document);
            impl->plan = std::move(reused.plan);
            return PreparedDocument(std::move(impl));
        }
        if (reused.status != MetadataReuseStatus::NotApplicable)
        {
            return std::nullopt;
        }
    }

    ++cache.m_impl->statistics.fullDocumentPreparations;
    normalizeLayerInitialCanvasSizes(document);
    if (!DocumentOperations::normalizeAndValidate(document))
    {
        setError(error,
            DocumentSerializer::tr("The project contains invalid "
                                   "operations or too much mask data."));
        return std::nullopt;
    }
    DocumentValidationStats validationStats;
    if (!validateDocument(document, schemaVersion, error, &validationStats))
    {
        return std::nullopt;
    }

    const PreparedPlan *basePlan = base ? &base->m_impl->plan : nullptr;
    const ImmutableBackings *trustedBackings =
        trusted ? &trusted->m_impl->backings : nullptr;
    document = DocumentFreezer(basePlan, trustedBackings).freeze(document);
    const ClipMaskTable clipMasks = buildClipMaskTable(
        document, basePlan, *cache.m_impl, maximumBytes, false);
    const BinaryMaskTable binaryMasks = buildBinaryMaskTable(
        document, basePlan, *cache.m_impl, maximumBytes, false);
    PreparedPlan plan;
    if (clipMasks.invalid || binaryMasks.invalid)
    {
        setError(error,
            DocumentSerializer::tr("The project contains invalid "
                                   "operations or too much mask data."));
        return std::nullopt;
    }
    if (clipMasks.tooLarge || binaryMasks.tooLarge
        || !serializedSizeWithinLimit(document,
            clipMasks,
            binaryMasks,
            basePlan,
            plan,
            *cache.m_impl,
            maximumBytes))
    {
        setError(
            error, DocumentSerializer::tr("The project is too large to save."));
        return std::nullopt;
    }
    plan.totalStrokeCount = validationStats.totalStrokeCount;
    plan.totalPointCount = validationStats.totalPointCount;
    plan.distinctMaskBytes = validationStats.distinctMaskBytes;

    auto impl = std::make_shared<PreparedDocument::Impl>();
    impl->document = std::move(document);
    impl->plan = std::move(plan);
    return PreparedDocument(std::move(impl));
}

DocumentSerializer::AppendStrokeResult DocumentSerializer::appendStroke(
    const PreparedDocument &base,
    const QUuid &layerId,
    const Stroke &stroke,
    SerializationCache &cache,
    qint64 maximumBytes)
{
    AppendStrokeResult result;
    if (!base.m_impl || !cache.m_impl || maximumBytes < 0
        || maximumBytes > DocumentLimits::maximumProjectBytes)
    {
        result.status = AppendStrokeStatus::Invalid;
        return result;
    }
    if (stroke.mode == StrokeMode::PixelSelection
        || stroke.mode == StrokeMode::Reframe)
    {
        return result;
    }

    const Document &baseDocument = base.m_impl->document;
    const PreparedPlan &basePlan = base.m_impl->plan;
    const Layer *baseLayer = baseDocument.layer(layerId);
    if (!baseLayer || baseLayer->kind != LayerKind::Paint
        || basePlan.totalStrokeCount != basePlan.strokes.size())
    {
        result.status = AppendStrokeStatus::Invalid;
        return result;
    }
    if (baseLayer->strokes.size() >= DocumentLimits::maximumStrokesPerLayer
        || basePlan.totalStrokeCount >= DocumentLimits::maximumTotalStrokes)
    {
        result.status = AppendStrokeStatus::StrokeLimit;
        return result;
    }
    if (stroke.points.size() > DocumentLimits::maximumPointsPerStroke
        || stroke.points.size()
               > DocumentLimits::maximumTotalPoints - basePlan.totalPointCount)
    {
        result.status = AppendStrokeStatus::PointLimit;
        return result;
    }
    if (basePlan.strokes.contains(stroke.id)
        || !isValidIncrementalStroke(stroke, baseDocument.size))
    {
        result.status = AppendStrokeStatus::Invalid;
        return result;
    }

    PreparedPlan plan = basePlan;
    Stroke frozen = freezeIncrementalStroke(stroke);
    qint64 serializedGrowth = 0;
    AppendStrokeStatus status = AppendStrokeStatus::Appended;
    if (!freezeIncrementalClipMask(stroke.clipMask,
            plan,
            *cache.m_impl,
            maximumBytes,
            serializedGrowth,
            frozen.clipMask,
            status)
        || !freezeIncrementalClipMask(stroke.fillMask,
            plan,
            *cache.m_impl,
            maximumBytes,
            serializedGrowth,
            frozen.fillMask,
            status))
    {
        result.status = status;
        return result;
    }

    ClipMaskTable clipMasks;
    if (!frozen.clipMask.isNull())
    {
        clipMasks.idByCacheKey.insert(frozen.clipMask.cacheKey(),
            plan.clipIdsByIdentity.value(frozen.clipMask.cacheKey()));
    }
    if (!frozen.fillMask.isNull())
    {
        clipMasks.idByCacheKey.insert(frozen.fillMask.cacheKey(),
            plan.clipIdsByIdentity.value(frozen.fillMask.cacheKey()));
    }
    const BinaryMaskTable binaryMasks;
    ++cache.m_impl->statistics.strokeSerializations;
    const qint64 strokeBytes =
        QJsonDocument(strokeToJson(frozen, clipMasks, binaryMasks))
            .toJson(QJsonDocument::Compact)
            .size();
    if ((!baseLayer->strokes.isEmpty()
            && !addSerializedBytes(serializedGrowth, 1, maximumBytes))
        || !addSerializedBytes(serializedGrowth, strokeBytes, maximumBytes))
    {
        result.status = AppendStrokeStatus::TooLarge;
        return result;
    }
    qint64 compactSize = basePlan.compactSize;
    if (!addSerializedBytes(compactSize, serializedGrowth, maximumBytes))
    {
        result.status = AppendStrokeStatus::TooLarge;
        return result;
    }

    const QString resolvedClipId =
        frozen.clipMask.isNull()
            ? QString()
            : plan.clipIdsByIdentity.value(frozen.clipMask.cacheKey());
    const QString resolvedFillId =
        frozen.fillMask.isNull()
            ? QString()
            : plan.clipIdsByIdentity.value(frozen.fillMask.cacheKey());
    plan.strokes.insert(frozen.id,
        StrokeMeta{frozen, resolvedClipId, resolvedFillId, {}, strokeBytes});
    plan.compactSize = compactSize;
    ++plan.totalStrokeCount;
    plan.totalPointCount += frozen.points.size();

    Document document = baseDocument;
    Layer *target = document.layer(layerId);
    if (!target)
    {
        result.status = AppendStrokeStatus::Invalid;
        return result;
    }
    target->strokes.append(frozen);

    auto impl = std::make_shared<PreparedDocument::Impl>();
    impl->document = std::move(document);
    impl->plan = std::move(plan);
    result.prepared = PreparedDocument(std::move(impl));
    result.status = AppendStrokeStatus::Appended;
    ++cache.m_impl->statistics.incrementalStrokeAppends;
    return result;
}

std::optional<DocumentSerializer::PreparedDocument>
DocumentSerializer::rebindActiveLayer(
    const PreparedDocument &prepared, const QUuid &activeLayerId)
{
    if (!prepared.m_impl)
    {
        return std::nullopt;
    }
    const Document &source = prepared.m_impl->document;
    const Layer *active = source.layer(activeLayerId);
    const bool hasPaintLayer = std::any_of(source.layers.cbegin(),
        source.layers.cend(),
        [](const Layer &layer)
        {
            return layer.kind == LayerKind::Paint;
        });
    const bool valid = hasPaintLayer
                           ? active && active->kind == LayerKind::Paint
                           : activeLayerId.isNull();
    if (!valid)
    {
        return std::nullopt;
    }
    if (source.activeLayerId == activeLayerId)
    {
        return prepared;
    }

    auto rebound = std::make_shared<PreparedDocument::Impl>(*prepared.m_impl);
    const qint64 previousRootBytes =
        QJsonDocument(rootToJson(rebound->document, {}, {}, {}))
            .toJson(QJsonDocument::Compact)
            .size();
    rebound->document.activeLayerId = activeLayerId;
    const qint64 currentRootBytes =
        QJsonDocument(rootToJson(rebound->document, {}, {}, {}))
            .toJson(QJsonDocument::Compact)
            .size();
    rebound->plan.compactSize += currentRootBytes - previousRootBytes;
    return PreparedDocument(std::move(rebound));
}

bool DocumentSerializer::save(
    const QString &filePath, const Document &document, QString *error)
{
    SerializationCache cache;
    const std::optional<PreparedDocument> prepared =
        prepare(document, cache, error);
    return prepared && save(filePath, *prepared, cache, error);
}

bool DocumentSerializer::save(const QString &filePath,
    const PreparedDocument &document,
    SerializationCache &cache,
    QString *error)
{
    if (!document.m_impl || !cache.m_impl)
    {
        setError(error,
            DocumentSerializer::tr("The project contains invalid "
                                   "operations or too much mask data."));
        return false;
    }
    if (document.m_impl->plan.compactSize <= 0
        || document.m_impl->plan.compactSize
               > DocumentLimits::maximumProjectBytes)
    {
        setError(
            error, DocumentSerializer::tr("The project is too large to save."));
        return false;
    }
    const QByteArray data = toJson(document, cache);
    if (data.isEmpty())
    {
        setError(error,
            DocumentSerializer::tr("The project contains invalid "
                                   "operations or too much mask data."));
        return false;
    }
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly))
    {
        setError(error, file.errorString());
        return false;
    }
    if (file.write(data) != data.size())
    {
        setError(error, file.errorString());
        file.cancelWriting();
        return false;
    }
    if (!file.commit())
    {
        setError(error, file.errorString());
        return false;
    }
    return true;
}

std::optional<Document> DocumentSerializer::load(
    const QString &filePath, QString *error)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        setError(error, file.errorString());
        return std::nullopt;
    }
    if (file.size() < 0 || file.size() > DocumentLimits::maximumProjectBytes)
    {
        setError(
            error, DocumentSerializer::tr("The project file is too large."));
        return std::nullopt;
    }
    return fromJson(file.readAll(), error);
}

QByteArray DocumentSerializer::toJson(const Document &document)
{
    return toJson(document, {});
}

QByteArray DocumentSerializer::toJson(
    const Document &document, const QJsonObject &additionalRootFields)
{
    SerializationCache cache;
    const std::optional<PreparedDocument> prepared = prepare(document, cache);
    return prepared ? toJson(*prepared, cache, additionalRootFields)
                    : QByteArray();
}

QByteArray DocumentSerializer::toJson(
    const PreparedDocument &document, SerializationCache &cache)
{
    return toJson(document, cache, {});
}

QByteArray DocumentSerializer::toJson(const PreparedDocument &document,
    SerializationCache &cache,
    const QJsonObject &additionalRootFields)
{
    if (!document.m_impl || !cache.m_impl
        || document.m_impl->plan.compactSize <= 0
        || document.m_impl->plan.compactSize
               > DocumentLimits::maximumProjectBytes)
    {
        return {};
    }
    const QJsonObject rootSkeleton =
        rootToJson(document.m_impl->document, {}, {}, {});
    for (auto field = additionalRootFields.constBegin();
        field != additionalRootFields.constEnd();
        ++field)
    {
        if (rootSkeleton.contains(field.key()))
        {
            return {};
        }
    }
    const std::optional<QByteArray> data =
        serializePreparedDocument(document.m_impl->document,
            document.m_impl->plan,
            *cache.m_impl,
            additionalRootFields);
    return data.value_or(QByteArray());
}

std::optional<Document> DocumentSerializer::fromJson(
    const QByteArray &data, QString *error)
{
    return fromJson(data, nullptr, error);
}

std::optional<Document> DocumentSerializer::fromJson(
    const QByteArray &data, QJsonObject *parsedRoot, QString *error)
{
    if (data.size() > DocumentLimits::maximumProjectBytes)
    {
        setError(
            error, DocumentSerializer::tr("The project data is too large."));
        return std::nullopt;
    }
    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(data, &parseError);
    if (json.isNull() || !json.isObject())
    {
        setError(error, parseError.errorString());
        return std::nullopt;
    }

    const QJsonObject root = json.object();
    const std::optional<int> fileSchemaVersion =
        integerFromJson(root.value(QStringLiteral("schemaVersion")));
    if (!fileSchemaVersion || *fileSchemaVersion < 1
        || *fileSchemaVersion > schemaVersion)
    {
        setError(error,
            DocumentSerializer::tr("This project version is not supported."));
        return std::nullopt;
    }
    const std::optional<int> fileAlgorithmVersion =
        integerFromJson(root.value(QStringLiteral("algorithmVersion")));
    if (!fileAlgorithmVersion || *fileAlgorithmVersion < 1
        || *fileAlgorithmVersion > algorithmVersion)
    {
        setError(error,
            DocumentSerializer::tr(
                "This rendering algorithm version is not supported."));
        return std::nullopt;
    }

    const QJsonValue activeLayerIdValue =
        root.value(QStringLiteral("activeLayerId"));
    if (!root.value(QStringLiteral("canvas")).isObject()
        || !root.value(QStringLiteral("animation")).isObject()
        || (!activeLayerIdValue.isString() && !activeLayerIdValue.isNull())
        || !root.value(QStringLiteral("layers")).isArray()
        || (*fileSchemaVersion >= 4
            && !root.value(QStringLiteral("clipMasks")).isArray())
        || (*fileSchemaVersion >= 6
            && !root.value(QStringLiteral("binaryMasks")).isArray()))
    {
        setError(error,
            DocumentSerializer::tr("The project contains invalid fields."));
        return std::nullopt;
    }
    const QJsonObject canvas = root.value(QStringLiteral("canvas")).toObject();
    const std::optional<int> width =
        integerFromJson(canvas.value(QStringLiteral("width")));
    const std::optional<int> height =
        integerFromJson(canvas.value(QStringLiteral("height")));
    if (!width || !height
        || !canvas.value(QStringLiteral("background")).isString()
        || *width < DocumentLimits::minimumCanvasEdge
        || *height < DocumentLimits::minimumCanvasEdge
        || *width > DocumentLimits::maximumCanvasEdge
        || *height > DocumentLimits::maximumCanvasEdge)
    {
        setError(error, DocumentSerializer::tr("The canvas size is invalid."));
        return std::nullopt;
    }
    const QColor background(
        canvas.value(QStringLiteral("background")).toString());
    if (!background.isValid())
    {
        setError(
            error, DocumentSerializer::tr("The canvas background is invalid."));
        return std::nullopt;
    }
    const QJsonArray layers = root.value(QStringLiteral("layers")).toArray();
    if (layers.size() > DocumentLimits::maximumLayers)
    {
        setError(error, DocumentSerializer::tr("The layer count is invalid."));
        return std::nullopt;
    }
    if (!validateCollectionBudgets(layers, error))
    {
        return std::nullopt;
    }

    quint64 remainingAssetBytes = DocumentLimits::maximumDistinctClipMaskBytes;
    QHash<QString, QImage> referencedMasks;
    if (*fileSchemaVersion >= 4)
    {
        const std::optional<QHash<QString, QImage>> parsedMasks =
            clipMaskTableFromJson(root.value(QStringLiteral("clipMasks")),
                QSize(*width, *height),
                *fileSchemaVersion <= 5,
                remainingAssetBytes,
                error);
        if (!parsedMasks)
        {
            return std::nullopt;
        }
        referencedMasks = *parsedMasks;
    }
    QHash<QString, PackedMaskRegion> referencedBinaryMasks;
    if (*fileSchemaVersion >= 6)
    {
        const std::optional<QHash<QString, PackedMaskRegion>> parsedMasks =
            binaryMaskTableFromJson(root.value(QStringLiteral("binaryMasks")),
                remainingAssetBytes,
                error);
        if (!parsedMasks)
        {
            return std::nullopt;
        }
        referencedBinaryMasks = *parsedMasks;
    }

    const QJsonObject animation =
        root.value(QStringLiteral("animation")).toObject();
    const std::optional<int> frames =
        integerFromJson(animation.value(QStringLiteral("frames")));
    if (!frames || !animation.value(QStringLiteral("fps")).isDouble()
        || !animation.value(QStringLiteral("wobble")).isDouble())
    {
        setError(error,
            DocumentSerializer::tr("The animation settings are invalid."));
        return std::nullopt;
    }
    const qreal framesPerSecond =
        animation.value(QStringLiteral("fps")).toDouble();
    const qreal wobbleAmount =
        animation.value(QStringLiteral("wobble")).toDouble();
    if (*frames < DocumentLimits::minimumAnimationFrames
        || *frames > DocumentLimits::maximumAnimationFrames
        || !std::isfinite(framesPerSecond)
        || framesPerSecond < DocumentLimits::minimumFramesPerSecond
        || framesPerSecond > DocumentLimits::maximumFramesPerSecond
        || !std::isfinite(wobbleAmount)
        || wobbleAmount < DocumentLimits::minimumWobbleAmount
        || wobbleAmount > DocumentLimits::maximumWobbleAmount)
    {
        setError(error,
            DocumentSerializer::tr("The animation settings are invalid."));
        return std::nullopt;
    }

    Document document;
    document.size = QSize(*width, *height);
    document.background = background;
    document.animationFrames = *frames;
    document.framesPerSecond = framesPerSecond;
    document.wobbleAmount = wobbleAmount;
    document.layers.reserve(layers.size());
    QSet<QUuid> layerIds;
    QSet<QUuid> strokeIds;
    qsizetype totalPoints = 0;
    QHash<QByteArray, QImage> maskCache;
    quint64 distinctMaskBytes = 0;

    for (const QJsonValue &layerValue : layers)
    {
        const std::optional<Layer> layer = layerFromJson(layerValue,
            *fileSchemaVersion,
            QSize(*width, *height),
            maskCache,
            referencedMasks,
            referencedBinaryMasks,
            distinctMaskBytes,
            error);
        if (!layer)
        {
            return std::nullopt;
        }
        if (layerIds.contains(layer->id))
        {
            setError(error,
                DocumentSerializer::tr(
                    "The project contains duplicate layer IDs."));
            return std::nullopt;
        }
        layerIds.insert(layer->id);
        for (const Stroke &stroke : layer->strokes)
        {
            if (stroke.points.size()
                > DocumentLimits::maximumTotalPoints - totalPoints)
            {
                setError(error,
                    DocumentSerializer::tr(
                        "The project contains too many points."));
                return std::nullopt;
            }
            totalPoints += stroke.points.size();
            if (strokeIds.contains(stroke.id))
            {
                setError(error,
                    DocumentSerializer::tr(
                        "The project contains duplicate stroke IDs."));
                return std::nullopt;
            }
            strokeIds.insert(stroke.id);
        }
        document.layers.append(*layer);
    }

    document.activeLayerId = activeLayerIdValue.isNull()
                                 ? QUuid()
                                 : QUuid(activeLayerIdValue.toString());
    if (activeLayerIdValue.isString() && document.activeLayerId.isNull())
    {
        setError(
            error, DocumentSerializer::tr("The active layer ID is invalid."));
        return std::nullopt;
    }
    if (!validateDocument(document, *fileSchemaVersion, error))
    {
        return std::nullopt;
    }
    if (!DocumentOperations::normalizeAndValidate(document))
    {
        setError(error,
            DocumentSerializer::tr("The project contains invalid "
                                   "operations or too much mask data."));
        return std::nullopt;
    }
    if (!validateDocument(document, schemaVersion, error))
    {
        return std::nullopt;
    }
    if (parsedRoot)
    {
        *parsedRoot = root;
    }
    return document;
}

}
