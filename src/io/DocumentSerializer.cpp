#include "io/DocumentSerializer.hpp"

#include "document/DocumentLimits.hpp"
#include "document/DocumentOperations.hpp"
#include "document/LayerHierarchy.hpp"
#include "document/SelectionOperation.hpp"

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

namespace serializer_detail
{

struct ClipAssetMeta
{
    QString id;
    QImage image;
    qint64 serializedEntryBytes = 0;
    qsizetype compressedBytes = 0;
};

struct BinaryAssetMeta
{
    QString id;
    PackedMaskRegion region;
    qint64 serializedEntryBytes = 0;
    qsizetype compressedBytes = 0;
};

struct StrokeMeta
{
    Stroke snapshot;
    QString clipMaskId;
    QString fillMaskId;
    QString binaryMaskId;
    qint64 serializedBytes = 0;
};

struct PreparedPlan
{
    QMap<QString, ClipAssetMeta> clipAssets;
    QMap<QString, BinaryAssetMeta> binaryAssets;
    QHash<qint64, QString> clipIdsByIdentity;
    QHash<QString, QString> binaryIdsByIdentity;
    QHash<QUuid, StrokeMeta> strokes;
    qint64 compactSize = 0;
    qsizetype totalStrokeCount = 0;
    qsizetype totalPointCount = 0;
    quint64 distinctMaskBytes = 0;
};

struct ImmutableBackings
{
    QHash<qint64, QImage> images;
    QHash<QString, QByteArray> byteArrays;
    QHash<QString, QVector<StrokePoint>> pointVectors;
};

}

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

using serializer_detail::BinaryAssetMeta;
using serializer_detail::ClipAssetMeta;
using serializer_detail::ImmutableBackings;
using serializer_detail::PreparedPlan;
using serializer_detail::StrokeMeta;

struct DocumentValidationStats
{
    qsizetype totalStrokeCount = 0;
    qsizetype totalPointCount = 0;
    quint64 distinctMaskBytes = 0;
};

constexpr int schemaVersion = 9;
constexpr int algorithmVersion = 2;
constexpr int serializationFormatGeneration = 1;

std::optional<int> integerFromJson(const QJsonValue &value);

void setError(QString *error, const QString &message)
{
    if (error)
    {
        *error = message;
    }
}

QByteArray canonicalMaskBytes(const QImage &mask)
{
    if (mask.isNull() || mask.format() != QImage::Format_Grayscale8)
    {
        return {};
    }
    const qint64 byteCount = static_cast<qint64>(mask.width()) * mask.height();
    if (byteCount <= 0 || byteCount > std::numeric_limits<int>::max())
    {
        return {};
    }
    QByteArray bytes(static_cast<qsizetype>(byteCount), '\0');
    for (int y = 0; y < mask.height(); ++y)
    {
        std::memcpy(bytes.data() + static_cast<qsizetype>(y) * mask.width(),
            mask.constScanLine(y),
            static_cast<std::size_t>(mask.width()));
    }
    return bytes;
}

QString maskContentId(int width, int height, const QByteArray &canonicalBytes)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(QByteArray::number(width));
    hash.addData(QByteArrayLiteral("x"));
    hash.addData(QByteArray::number(height));
    hash.addData(QByteArrayLiteral(":"));
    hash.addData(canonicalBytes);
    return QString::fromLatin1(hash.result().toHex());
}

struct SerializedClipMask
{
    QString id;
    QImage image;
    QByteArray compressedData;
    qsizetype compressedBytes = 0;
    qint64 serializedBytes = 0;
};

struct ClipMaskTable
{
    QHash<qint64, QString> idByCacheKey;
    QMap<QString, SerializedClipMask> entries;
    qint64 serializedEntryBytes = 0;
    bool tooLarge = false;
    bool invalid = false;
};

qint64 base64Size(qsizetype byteCount)
{
    if (byteCount < 0)
    {
        return -1;
    }
    return 4LL * ((static_cast<qint64>(byteCount) + 2LL) / 3LL);
}

QString payloadCacheKey(bool binary, const QString &contentId)
{
    return QStringLiteral("%1:%2:qcompress-6:%3")
        .arg(serializationFormatGeneration)
        .arg(binary ? QStringLiteral("binary") : QStringLiteral("clip"))
        .arg(contentId);
}

template <typename Cache>
QByteArray compressedPayload(Cache &cache,
    bool binary,
    const QString &contentId,
    const QByteArray &source)
{
    const QString key = payloadCacheKey(binary, contentId);
    QByteArray compressed = cache.payload(key);
    if (!compressed.isEmpty())
    {
        return compressed;
    }
    if (binary)
    {
        ++cache.statistics.binaryMaskCompressions;
    }
    else
    {
        ++cache.statistics.clipMaskCompressions;
    }
    compressed = qCompress(source, 6);
    cache.storePayload(key, compressed);
    return compressed;
}

QJsonObject serializedClipMaskToJson(const SerializedClipMask &entry)
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), entry.id);
    object.insert(QStringLiteral("width"), entry.image.width());
    object.insert(QStringLiteral("height"), entry.image.height());
    object.insert(QStringLiteral("data"),
        QString::fromLatin1(entry.compressedData.toBase64()));
    return object;
}

qint64 serializedClipMaskSize(
    const QString &id, const QImage &image, qsizetype compressedBytes)
{
    SerializedClipMask skeleton;
    skeleton.id = id;
    skeleton.image = image;
    const qint64 skeletonBytes =
        QJsonDocument(serializedClipMaskToJson(skeleton))
            .toJson(QJsonDocument::Compact)
            .size();
    const qint64 encodedBytes = base64Size(compressedBytes);
    return encodedBytes < 0 ? -1 : skeletonBytes + encodedBytes;
}

QHash<qint64, ClipAssetMeta> clipAssetsByIdentity(const PreparedPlan *base)
{
    QHash<qint64, ClipAssetMeta> result;
    if (!base)
    {
        return result;
    }
    for (auto identity = base->clipIdsByIdentity.cbegin();
        identity != base->clipIdsByIdentity.cend();
        ++identity)
    {
        const auto asset = base->clipAssets.constFind(identity.value());
        if (asset != base->clipAssets.cend())
        {
            result.insert(identity.key(), asset.value());
        }
    }
    return result;
}

template <typename Cache>
QString registerClipMask(const QImage &mask,
    ClipMaskTable &table,
    const QHash<qint64, ClipAssetMeta> &baseAssets,
    Cache &cache,
    qint64 maximumBytes,
    bool requirePayload)
{
    if (mask.isNull())
    {
        return {};
    }
    const qint64 cacheKey = mask.cacheKey();
    const auto cachedId = table.idByCacheKey.constFind(cacheKey);
    if (cachedId != table.idByCacheKey.cend())
    {
        return cachedId.value();
    }

    const auto baseAsset = baseAssets.constFind(cacheKey);
    if (baseAsset != baseAssets.cend() && baseAsset->image.size() == mask.size()
        && baseAsset->image.format() == mask.format())
    {
        const auto existing = table.entries.constFind(baseAsset->id);
        if (existing != table.entries.cend())
        {
            table.idByCacheKey.insert(cacheKey, baseAsset->id);
            return baseAsset->id;
        }
        SerializedClipMask entry;
        entry.id = baseAsset->id;
        entry.image = mask;
        entry.serializedBytes = baseAsset->serializedEntryBytes;
        entry.compressedBytes = baseAsset->compressedBytes;
        if (requirePayload)
        {
            const QString key = payloadCacheKey(false, entry.id);
            entry.compressedData = cache.payload(key);
            if (entry.compressedData.isEmpty())
            {
                const QByteArray bytes = canonicalMaskBytes(mask);
                if (bytes.isEmpty())
                {
                    table.invalid = true;
                    return {};
                }
                ++cache.statistics.clipMaskCompressions;
                entry.compressedData = qCompress(bytes, 6);
                cache.storePayload(key, entry.compressedData);
            }
            if (entry.compressedData.size() != baseAsset->compressedBytes
                || serializedClipMaskSize(
                       entry.id, entry.image, entry.compressedData.size())
                       != entry.serializedBytes)
            {
                table.invalid = true;
                return {};
            }
        }
        if (entry.serializedBytes < 0 || entry.serializedBytes > maximumBytes
            || table.serializedEntryBytes
                   > maximumBytes - entry.serializedBytes)
        {
            table.tooLarge = true;
            return {};
        }
        table.serializedEntryBytes += entry.serializedBytes;
        table.idByCacheKey.insert(cacheKey, entry.id);
        table.entries.insert(entry.id, std::move(entry));
        return baseAsset->id;
    }

    const QByteArray bytes = canonicalMaskBytes(mask);
    if (bytes.isEmpty())
    {
        table.invalid = true;
        return {};
    }
    ++cache.statistics.clipMaskContentHashes;
    const QString id = maskContentId(mask.width(), mask.height(), bytes);
    const auto existing = table.entries.constFind(id);
    if (existing != table.entries.cend())
    {
        if (existing->image.size() != mask.size() || existing->image != mask)
        {
            table.invalid = true;
            return {};
        }
        table.idByCacheKey.insert(cacheKey, id);
        return id;
    }

    const QByteArray compressed = compressedPayload(cache, false, id, bytes);
    if (compressed.isEmpty())
    {
        table.invalid = true;
        return {};
    }
    SerializedClipMask entry;
    entry.id = id;
    entry.image = mask;
    entry.serializedBytes = serializedClipMaskSize(id, mask, compressed.size());
    entry.compressedBytes = compressed.size();
    if (requirePayload)
    {
        entry.compressedData = compressed;
    }
    const qint64 entryBytes = entry.serializedBytes;
    if (entryBytes > maximumBytes || entryBytes < 0
        || table.serializedEntryBytes > maximumBytes - entryBytes)
    {
        table.tooLarge = true;
        return {};
    }
    table.serializedEntryBytes += entryBytes;
    table.idByCacheKey.insert(cacheKey, id);
    table.entries.insert(id, std::move(entry));
    return id;
}

template <typename Cache>
ClipMaskTable buildClipMaskTable(const Document &document,
    const PreparedPlan *base,
    Cache &cache,
    qint64 maximumBytes,
    bool requirePayload)
{
    ClipMaskTable table;
    const QHash<qint64, ClipAssetMeta> baseAssets = clipAssetsByIdentity(base);
    for (const Layer &layer : document.layers)
    {
        for (const Stroke &stroke : layer.strokes)
        {
            if ((!stroke.clipMask.isNull()
                    && registerClipMask(stroke.clipMask,
                        table,
                        baseAssets,
                        cache,
                        maximumBytes,
                        requirePayload)
                        .isEmpty())
                || (!stroke.fillMask.isNull()
                    && registerClipMask(stroke.fillMask,
                        table,
                        baseAssets,
                        cache,
                        maximumBytes,
                        requirePayload)
                        .isEmpty()))
            {
                return table;
            }
        }
    }
    return table;
}

QString binaryMaskContentId(const PackedMaskRegion &region)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(QByteArrayLiteral("binary-mask:"));
    for (const int value : {region.canvasSize.width(),
             region.canvasSize.height(),
             region.bounds.x(),
             region.bounds.y(),
             region.bounds.width(),
             region.bounds.height()})
    {
        hash.addData(QByteArray::number(value));
        hash.addData(QByteArrayLiteral(":"));
    }
    hash.addData(region.packedMask);
    return QString::fromLatin1(hash.result().toHex());
}

struct SerializedBinaryMask
{
    QString id;
    PackedMaskRegion region;
    QByteArray compressedData;
    qsizetype compressedBytes = 0;
    qint64 serializedBytes = 0;
};

struct BinaryMaskTable
{
    QHash<QString, QString> idByIdentity;
    QMap<QString, SerializedBinaryMask> entries;
    qint64 serializedEntryBytes = 0;
    bool tooLarge = false;
    bool invalid = false;
};

QJsonObject serializedBinaryMaskToJson(const SerializedBinaryMask &entry)
{
    const PackedMaskRegion &region = entry.region;
    QJsonObject object;
    object.insert(QStringLiteral("id"), entry.id);
    object.insert(QStringLiteral("canvas"),
        QJsonArray{region.canvasSize.width(), region.canvasSize.height()});
    object.insert(QStringLiteral("bounds"),
        QJsonArray{region.bounds.x(),
            region.bounds.y(),
            region.bounds.width(),
            region.bounds.height()});
    object.insert(QStringLiteral("data"),
        QString::fromLatin1(entry.compressedData.toBase64()));
    return object;
}

qint64 serializedBinaryMaskSize(const QString &id,
    const PackedMaskRegion &region,
    qsizetype compressedBytes)
{
    SerializedBinaryMask skeleton;
    skeleton.id = id;
    skeleton.region = region;
    const qint64 skeletonBytes =
        QJsonDocument(serializedBinaryMaskToJson(skeleton))
            .toJson(QJsonDocument::Compact)
            .size();
    const qint64 encodedBytes = base64Size(compressedBytes);
    return encodedBytes < 0 ? -1 : skeletonBytes + encodedBytes;
}

QString binaryMaskIdentity(const PackedMaskRegion &region)
{
    return QStringLiteral("%1:%2:%3:%4:%5:%6:%7:%8")
        .arg(reinterpret_cast<quintptr>(region.packedMask.constData()), 0, 16)
        .arg(region.packedMask.size())
        .arg(region.canvasSize.width())
        .arg(region.canvasSize.height())
        .arg(region.bounds.x())
        .arg(region.bounds.y())
        .arg(region.bounds.width())
        .arg(region.bounds.height());
}

QHash<QString, BinaryAssetMeta> binaryAssetsByIdentity(const PreparedPlan *base)
{
    QHash<QString, BinaryAssetMeta> result;
    if (!base)
    {
        return result;
    }
    for (auto identity = base->binaryIdsByIdentity.cbegin();
        identity != base->binaryIdsByIdentity.cend();
        ++identity)
    {
        const auto asset = base->binaryAssets.constFind(identity.value());
        if (asset != base->binaryAssets.cend())
        {
            result.insert(identity.key(), asset.value());
        }
    }
    return result;
}

template <typename Cache>
QString registerBinaryMask(const PackedMaskRegion &region,
    BinaryMaskTable &table,
    const QHash<QString, BinaryAssetMeta> &baseAssets,
    Cache &cache,
    qint64 maximumBytes,
    bool requirePayload)
{
    if (!isValidPackedMaskRegion(region))
    {
        table.invalid = true;
        return {};
    }
    const QString identity = binaryMaskIdentity(region);
    const auto knownIdentity = table.idByIdentity.constFind(identity);
    if (knownIdentity != table.idByIdentity.cend())
    {
        return knownIdentity.value();
    }

    const auto baseAsset = baseAssets.constFind(identity);
    if (baseAsset != baseAssets.cend())
    {
        const auto existing = table.entries.constFind(baseAsset->id);
        if (existing != table.entries.cend())
        {
            table.idByIdentity.insert(identity, baseAsset->id);
            return baseAsset->id;
        }
        SerializedBinaryMask entry;
        entry.id = baseAsset->id;
        entry.region = region;
        entry.serializedBytes = baseAsset->serializedEntryBytes;
        entry.compressedBytes = baseAsset->compressedBytes;
        if (requirePayload)
        {
            entry.compressedData =
                compressedPayload(cache, true, entry.id, region.packedMask);
            if (entry.compressedData.size() != baseAsset->compressedBytes
                || serializedBinaryMaskSize(
                       entry.id, entry.region, entry.compressedData.size())
                       != entry.serializedBytes)
            {
                table.invalid = true;
                return {};
            }
        }
        if (entry.serializedBytes < 0 || entry.serializedBytes > maximumBytes
            || table.serializedEntryBytes
                   > maximumBytes - entry.serializedBytes)
        {
            table.tooLarge = true;
            return {};
        }
        table.serializedEntryBytes += entry.serializedBytes;
        table.idByIdentity.insert(identity, entry.id);
        table.entries.insert(entry.id, std::move(entry));
        return baseAsset->id;
    }

    ++cache.statistics.binaryMaskContentHashes;
    const QString id = binaryMaskContentId(region);
    const auto existing = table.entries.constFind(id);
    if (existing != table.entries.cend())
    {
        if (existing->region != region)
        {
            table.invalid = true;
            return {};
        }
        table.idByIdentity.insert(identity, id);
        return id;
    }
    const QByteArray compressed =
        compressedPayload(cache, true, id, region.packedMask);
    if (compressed.isEmpty())
    {
        table.invalid = true;
        return {};
    }
    SerializedBinaryMask entry;
    entry.id = id;
    entry.region = region;
    entry.serializedBytes =
        serializedBinaryMaskSize(id, region, compressed.size());
    entry.compressedBytes = compressed.size();
    if (requirePayload)
    {
        entry.compressedData = compressed;
    }
    const qint64 entryBytes = entry.serializedBytes;
    if (entryBytes > maximumBytes || entryBytes < 0
        || table.serializedEntryBytes > maximumBytes - entryBytes)
    {
        table.tooLarge = true;
        return {};
    }
    table.serializedEntryBytes += entryBytes;
    table.idByIdentity.insert(identity, id);
    table.entries.insert(id, std::move(entry));
    return id;
}

PackedMaskRegion pixelSelectionMaskRegion(const PixelSelectionOp &operation)
{
    return PackedMaskRegion{
        operation.canvasSize, operation.sourceBounds, operation.packedMask};
}

template <typename Cache>
BinaryMaskTable buildBinaryMaskTable(const Document &document,
    const PreparedPlan *base,
    Cache &cache,
    qint64 maximumBytes,
    bool requirePayload)
{
    BinaryMaskTable table;
    const QHash<QString, BinaryAssetMeta> baseAssets =
        binaryAssetsByIdentity(base);
    for (const Layer &layer : document.layers)
    {
        for (const Stroke &stroke : layer.strokes)
        {
            if (stroke.pixelSelectionOp
                && registerBinaryMask(
                    pixelSelectionMaskRegion(*stroke.pixelSelectionOp),
                    table,
                    baseAssets,
                    cache,
                    maximumBytes,
                    requirePayload)
                    .isEmpty())
            {
                return table;
            }
        }
    }
    return table;
}

QJsonArray pointToJson(const StrokePoint &point)
{
    return {point.position.x(), point.position.y(), point.pressure};
}

std::optional<StrokePoint> pointFromJson(const QJsonValue &value)
{
    if (!value.isArray())
    {
        return std::nullopt;
    }
    const QJsonArray array = value.toArray();
    if ((array.size() != 2 && array.size() != 3) || !array[0].isDouble()
        || !array[1].isDouble() || (array.size() == 3 && !array[2].isDouble()))
    {
        return std::nullopt;
    }
    StrokePoint point;
    point.position = QPointF(array[0].toDouble(), array[1].toDouble());
    point.pressure = array.size() == 3 ? array[2].toDouble() : 1.0;
    if (!std::isfinite(point.position.x()) || !std::isfinite(point.position.y())
        || !std::isfinite(point.pressure) || point.pressure < 0.0
        || point.pressure > 1.0)
    {
        return std::nullopt;
    }
    return point;
}

QJsonObject brushToJson(const BrushSettings &brush)
{
    QString engineName = QStringLiteral("line");
    if (brush.engine == BrushEngine::Airbrush)
    {
        engineName = QStringLiteral("airbrush");
    }
    else if (brush.engine == BrushEngine::Spray)
    {
        engineName = QStringLiteral("spray");
    }
    const QString tipName = brush.tipShape == BrushTipShape::Square
                                ? QStringLiteral("square")
                                : QStringLiteral("round");

    QJsonObject object;
    object.insert(QStringLiteral("engine"), engineName);
    object.insert(QStringLiteral("tip"), tipName);
    object.insert(QStringLiteral("opacity"), brush.opacity);
    object.insert(QStringLiteral("flow"), brush.flow);
    object.insert(QStringLiteral("hardness"), brush.hardness);
    object.insert(QStringLiteral("spacing"), brush.spacing);
    object.insert(QStringLiteral("scatter"), brush.scatter);
    object.insert(QStringLiteral("particleSize"), brush.particleSize);
    object.insert(QStringLiteral("density"), brush.density);
    object.insert(QStringLiteral("sizeDynamics"), brush.sizeDynamics);
    object.insert(QStringLiteral("opacityDynamics"), brush.opacityDynamics);
    object.insert(QStringLiteral("sizeJitter"), brush.sizeJitter);
    object.insert(QStringLiteral("animatedJitter"), brush.animatedJitter);
    object.insert(QStringLiteral("wobbleScale"), brush.wobbleScale);
    object.insert(QStringLiteral("antialiasing"), brush.antialiasing);
    return object;
}

std::optional<BrushSettings> brushFromJson(
    const QJsonValue &value, QString *error)
{
    if (!value.isObject())
    {
        setError(error,
            DocumentSerializer::tr("A stroke has invalid brush settings."));
        return std::nullopt;
    }
    const QJsonObject object = value.toObject();
    if (!object.value(QStringLiteral("engine")).isString()
        || !object.value(QStringLiteral("tip")).isString()
        || !object.value(QStringLiteral("opacity")).isDouble()
        || !object.value(QStringLiteral("flow")).isDouble()
        || !object.value(QStringLiteral("hardness")).isDouble()
        || !object.value(QStringLiteral("spacing")).isDouble()
        || !object.value(QStringLiteral("scatter")).isDouble()
        || !object.value(QStringLiteral("particleSize")).isDouble()
        || !object.value(QStringLiteral("density")).isDouble()
        || !object.value(QStringLiteral("sizeDynamics")).isDouble()
        || !object.value(QStringLiteral("opacityDynamics")).isDouble()
        || !object.value(QStringLiteral("sizeJitter")).isDouble()
        || !object.value(QStringLiteral("animatedJitter")).isBool())
    {
        setError(error,
            DocumentSerializer::tr("A stroke has invalid brush settings."));
        return std::nullopt;
    }
    const QJsonValue wobbleScaleValue =
        object.value(QStringLiteral("wobbleScale"));
    if (!wobbleScaleValue.isUndefined() && !wobbleScaleValue.isDouble())
    {
        setError(error,
            DocumentSerializer::tr("A stroke has invalid brush settings."));
        return std::nullopt;
    }
    const QJsonValue antialiasingValue =
        object.value(QStringLiteral("antialiasing"));
    if (!antialiasingValue.isUndefined() && !antialiasingValue.isBool())
    {
        setError(error,
            DocumentSerializer::tr("A stroke has invalid brush settings."));
        return std::nullopt;
    }

    BrushSettings brush;
    const QString engine = object.value(QStringLiteral("engine")).toString();
    if (engine == QStringLiteral("airbrush"))
    {
        brush.engine = BrushEngine::Airbrush;
    }
    else if (engine == QStringLiteral("spray"))
    {
        brush.engine = BrushEngine::Spray;
    }
    else if (engine != QStringLiteral("line"))
    {
        setError(error,
            DocumentSerializer::tr("A stroke has an invalid brush engine."));
        return std::nullopt;
    }
    const QString tip = object.value(QStringLiteral("tip")).toString();
    if (tip == QStringLiteral("square"))
    {
        brush.tipShape = BrushTipShape::Square;
    }
    else if (tip != QStringLiteral("round"))
    {
        setError(error,
            DocumentSerializer::tr("A stroke has an invalid brush tip."));
        return std::nullopt;
    }
    brush.opacity = object.value(QStringLiteral("opacity")).toDouble();
    brush.flow = object.value(QStringLiteral("flow")).toDouble();
    brush.hardness = object.value(QStringLiteral("hardness")).toDouble();
    brush.spacing = object.value(QStringLiteral("spacing")).toDouble();
    brush.scatter = object.value(QStringLiteral("scatter")).toDouble();
    brush.particleSize =
        object.value(QStringLiteral("particleSize")).toDouble();
    brush.density = object.value(QStringLiteral("density")).toDouble();
    brush.sizeDynamics =
        object.value(QStringLiteral("sizeDynamics")).toDouble();
    brush.opacityDynamics =
        object.value(QStringLiteral("opacityDynamics")).toDouble();
    brush.sizeJitter = object.value(QStringLiteral("sizeJitter")).toDouble();
    brush.animatedJitter =
        object.value(QStringLiteral("animatedJitter")).toBool();
    brush.wobbleScale =
        wobbleScaleValue.isDouble() ? wobbleScaleValue.toDouble() : 1.0;
    brush.antialiasing = antialiasingValue.toBool();
    if (!isValidBrushSettings(brush))
    {
        setError(error,
            DocumentSerializer::tr("A stroke has invalid brush settings."));
        return std::nullopt;
    }
    return brush;
}

QString samplingModeName(SamplingMode sampling)
{
    return sampling == SamplingMode::Smooth ? QStringLiteral("smooth")
                                            : QStringLiteral("nearest");
}

QString layerBlendModeName(LayerBlendMode mode)
{
    switch (mode)
    {
    case LayerBlendMode::Multiply:
        return QStringLiteral("multiply");
    case LayerBlendMode::Screen:
        return QStringLiteral("screen");
    case LayerBlendMode::Overlay:
        return QStringLiteral("overlay");
    case LayerBlendMode::Normal:
        return QStringLiteral("normal");
    }
    return {};
}

QString layerKindName(LayerKind kind)
{
    return kind == LayerKind::Group ? QStringLiteral("group")
                                    : QStringLiteral("paint");
}

QJsonArray transformToJson(const QTransform &transform)
{
    return {transform.m11(),
        transform.m12(),
        transform.m13(),
        transform.m21(),
        transform.m22(),
        transform.m23(),
        transform.m31(),
        transform.m32(),
        transform.m33()};
}

QJsonObject strokeToJson(const Stroke &stroke,
    const ClipMaskTable &clipMasks,
    const BinaryMaskTable &binaryMasks)
{
    QJsonArray points;
    for (const StrokePoint &point : stroke.points)
    {
        points.append(pointToJson(point));
    }

    QJsonObject object;
    object.insert(
        QStringLiteral("id"), stroke.id.toString(QUuid::WithoutBraces));
    object.insert(QStringLiteral("seed"), QString::number(stroke.seed));
    QString modeName = QStringLiteral("paint");
    if (stroke.mode == StrokeMode::Erase)
    {
        modeName = QStringLiteral("erase");
    }
    else if (stroke.mode == StrokeMode::Fill)
    {
        modeName = QStringLiteral("fill");
    }
    else if (stroke.mode == StrokeMode::PixelSelection)
    {
        modeName = QStringLiteral("pixelSelection");
    }
    else if (stroke.mode == StrokeMode::Reframe)
    {
        modeName = QStringLiteral("reframe");
    }
    object.insert(QStringLiteral("mode"), modeName);
    object.insert(QStringLiteral("color"), stroke.color.name(QColor::HexArgb));
    object.insert(QStringLiteral("width"), stroke.width);
    object.insert(QStringLiteral("brush"), brushToJson(stroke.brush));
    object.insert(QStringLiteral("points"), points);
    if (!stroke.clipMask.isNull())
    {
        const auto id =
            clipMasks.idByCacheKey.constFind(stroke.clipMask.cacheKey());
        if (id != clipMasks.idByCacheKey.cend())
        {
            object.insert(QStringLiteral("clipMaskId"), id.value());
        }
    }
    if (stroke.visibilityClip)
    {
        object.insert(QStringLiteral("visibilityClip"),
            QJsonArray{stroke.visibilityClip->x(),
                stroke.visibilityClip->y(),
                stroke.visibilityClip->width(),
                stroke.visibilityClip->height()});
    }
    if (!stroke.fillMask.isNull())
    {
        const auto id =
            clipMasks.idByCacheKey.constFind(stroke.fillMask.cacheKey());
        if (id != clipMasks.idByCacheKey.cend())
        {
            object.insert(QStringLiteral("fillMaskId"), id.value());
        }
    }
    if (stroke.pixelSelectionOp)
    {
        const PixelSelectionOp &operation = *stroke.pixelSelectionOp;
        QJsonObject payload;
        const auto maskId = binaryMasks.idByIdentity.constFind(
            binaryMaskIdentity(pixelSelectionMaskRegion(operation)));
        if (maskId != binaryMasks.idByIdentity.cend())
        {
            payload.insert(QStringLiteral("maskId"), maskId.value());
        }
        payload.insert(
            QStringLiteral("transform"), transformToJson(operation.transform));
        payload.insert(
            QStringLiteral("sampling"), samplingModeName(operation.sampling));
        payload.insert(QStringLiteral("clearSource"), operation.clearSource);
        payload.insert(
            QStringLiteral("drawDestination"), operation.drawDestination);
        object.insert(QStringLiteral("pixelSelection"), payload);
    }
    if (stroke.reframeOp)
    {
        const ReframeOp &operation = *stroke.reframeOp;
        QJsonObject payload;
        payload.insert(QStringLiteral("mode"),
            operation.mode == ReframeMode::Image ? QStringLiteral("image")
                                                 : QStringLiteral("canvas"));
        payload.insert(
            QStringLiteral("sampling"), samplingModeName(operation.sampling));
        payload.insert(QStringLiteral("sourceSize"),
            QJsonArray{
                operation.sourceSize.width(), operation.sourceSize.height()});
        payload.insert(QStringLiteral("targetSize"),
            QJsonArray{
                operation.targetSize.width(), operation.targetSize.height()});
        payload.insert(QStringLiteral("contentOffset"),
            QJsonArray{
                operation.contentOffset.x(), operation.contentOffset.y()});
        object.insert(QStringLiteral("reframe"), payload);
    }
    return object;
}

std::optional<QImage> legacyClipMaskFromJson(const QJsonValue &value,
    QHash<QByteArray, QImage> &maskCache,
    quint64 &distinctMaskBytes,
    QString *error)
{
    if (!value.isObject())
    {
        setError(error,
            DocumentSerializer::tr("A stroke has an invalid clip mask."));
        return std::nullopt;
    }
    const QJsonObject object = value.toObject();
    const std::optional<int> width =
        integerFromJson(object.value(QStringLiteral("width")));
    const std::optional<int> height =
        integerFromJson(object.value(QStringLiteral("height")));
    if (!width || !height || !object.value(QStringLiteral("data")).isString()
        || *width < DocumentLimits::minimumCanvasEdge
        || *height < DocumentLimits::minimumCanvasEdge
        || *width > DocumentLimits::maximumCanvasEdge
        || *height > DocumentLimits::maximumCanvasEdge)
    {
        setError(error,
            DocumentSerializer::tr("A stroke has an invalid clip mask."));
        return std::nullopt;
    }

    const QByteArray compressed = QByteArray::fromBase64(
        object.value(QStringLiteral("data")).toString().toLatin1());
    if (compressed.size() < 4)
    {
        setError(error,
            DocumentSerializer::tr("A stroke has an invalid clip mask."));
        return std::nullopt;
    }
    const auto *header =
        reinterpret_cast<const uchar *>(compressed.constData());
    const quint32 declaredSize = (static_cast<quint32>(header[0]) << 24U)
                                 | (static_cast<quint32>(header[1]) << 16U)
                                 | (static_cast<quint32>(header[2]) << 8U)
                                 | static_cast<quint32>(header[3]);
    const quint64 expectedSize =
        static_cast<quint64>((*width + 3) & ~3) * static_cast<quint64>(*height);
    if (expectedSize > std::numeric_limits<quint32>::max()
        || declaredSize != expectedSize)
    {
        setError(error,
            DocumentSerializer::tr("A stroke has an invalid clip mask."));
        return std::nullopt;
    }
    QByteArray cacheKey = QByteArray::number(*width) + 'x'
                          + QByteArray::number(*height) + ':' + compressed;
    const auto cached = maskCache.constFind(cacheKey);
    if (cached != maskCache.cend())
    {
        return cached.value();
    }
    if (declaredSize
        > DocumentLimits::maximumDistinctClipMaskBytes - distinctMaskBytes)
    {
        setError(error,
            DocumentSerializer::tr(
                "The project contains too much selection data."));
        return std::nullopt;
    }
    QImage mask(QSize(*width, *height), QImage::Format_Grayscale8);
    if (mask.isNull()
        || static_cast<quint64>(mask.sizeInBytes()) != expectedSize)
    {
        setError(
            error, DocumentSerializer::tr("A stroke clip mask is too large."));
        return std::nullopt;
    }
    const QByteArray bytes = qUncompress(compressed);
    if (bytes.size() != mask.sizeInBytes())
    {
        setError(error,
            DocumentSerializer::tr("A stroke has an invalid clip mask."));
        return std::nullopt;
    }
    std::memcpy(
        mask.bits(), bytes.constData(), static_cast<std::size_t>(bytes.size()));
    for (int y = 0; y < mask.height(); ++y)
    {
        std::fill(mask.scanLine(y) + mask.width(),
            mask.scanLine(y) + mask.bytesPerLine(),
            0);
    }
    distinctMaskBytes += declaredSize;
    maskCache.insert(std::move(cacheKey), mask);
    return mask;
}

bool isValidMaskContentId(const QString &id)
{
    return id.size() == 64
           && std::all_of(id.cbegin(),
               id.cend(),
               [](QChar character)
               {
                   return (character >= QLatin1Char('0')
                              && character <= QLatin1Char('9'))
                          || (character >= QLatin1Char('a')
                              && character <= QLatin1Char('f'));
               });
}

std::optional<QHash<QString, QImage>> clipMaskTableFromJson(
    const QJsonValue &value,
    const QSize &canvasSize,
    bool requireCanvasSize,
    quint64 &remainingAssetBytes,
    QString *error)
{
    if (!value.isArray())
    {
        setError(error,
            DocumentSerializer::tr(
                "The project contains an invalid selection mask table."));
        return std::nullopt;
    }
    const QJsonArray entries = value.toArray();
    if (entries.size() > DocumentLimits::maximumTotalStrokes)
    {
        setError(error,
            DocumentSerializer::tr(
                "The project contains too many selection masks."));
        return std::nullopt;
    }

    QHash<QString, QImage> masks;
    masks.reserve(entries.size());
    for (const QJsonValue &entryValue : entries)
    {
        if (!entryValue.isObject())
        {
            setError(error,
                DocumentSerializer::tr(
                    "The project contains an invalid selection mask table."));
            return std::nullopt;
        }
        const QJsonObject entry = entryValue.toObject();
        const QString id = entry.value(QStringLiteral("id")).toString();
        const std::optional<int> width =
            integerFromJson(entry.value(QStringLiteral("width")));
        const std::optional<int> height =
            integerFromJson(entry.value(QStringLiteral("height")));
        if (!entry.value(QStringLiteral("id")).isString()
            || !isValidMaskContentId(id) || !width || !height
            || (requireCanvasSize
                    ? QSize(*width, *height) != canvasSize
                    : *width < DocumentLimits::minimumCanvasEdge
                          || *height < DocumentLimits::minimumCanvasEdge
                          || *width > DocumentLimits::maximumCanvasEdge
                          || *height > DocumentLimits::maximumCanvasEdge)
            || !entry.value(QStringLiteral("data")).isString()
            || masks.contains(id))
        {
            setError(error,
                DocumentSerializer::tr(
                    "The project contains an invalid selection mask table."));
            return std::nullopt;
        }

        const QByteArray compressed = QByteArray::fromBase64(
            entry.value(QStringLiteral("data")).toString().toLatin1());
        if (compressed.size() < 4)
        {
            setError(error,
                DocumentSerializer::tr(
                    "The project contains an invalid selection mask."));
            return std::nullopt;
        }
        const auto *header =
            reinterpret_cast<const uchar *>(compressed.constData());
        const quint32 declaredSize = (static_cast<quint32>(header[0]) << 24U)
                                     | (static_cast<quint32>(header[1]) << 16U)
                                     | (static_cast<quint32>(header[2]) << 8U)
                                     | static_cast<quint32>(header[3]);
        const quint64 canonicalSize =
            static_cast<quint64>(*width) * static_cast<quint64>(*height);
        const quint64 paddedSize = static_cast<quint64>((*width + 3) & ~3)
                                   * static_cast<quint64>(*height);
        if (canonicalSize > std::numeric_limits<quint32>::max()
            || declaredSize != canonicalSize)
        {
            setError(error,
                DocumentSerializer::tr(
                    "The project contains an invalid selection mask."));
            return std::nullopt;
        }

        if (paddedSize > remainingAssetBytes)
        {
            setError(error,
                DocumentSerializer::tr(
                    "The project contains too much selection data."));
            return std::nullopt;
        }
        QImage mask(QSize(*width, *height), QImage::Format_Grayscale8);
        if (mask.isNull()
            || static_cast<quint64>(mask.sizeInBytes()) != paddedSize)
        {
            setError(error,
                DocumentSerializer::tr(
                    "The project contains an invalid selection mask."));
            return std::nullopt;
        }
        const QByteArray bytes = qUncompress(compressed);
        if (static_cast<quint64>(bytes.size()) != canonicalSize
            || maskContentId(*width, *height, bytes) != id)
        {
            setError(error,
                DocumentSerializer::tr(
                    "The project contains an invalid selection mask."));
            return std::nullopt;
        }
        mask.fill(0);
        for (int y = 0; y < *height; ++y)
        {
            std::memcpy(mask.scanLine(y),
                bytes.constData() + static_cast<qsizetype>(y) * *width,
                static_cast<std::size_t>(*width));
        }
        remainingAssetBytes -= mask.sizeInBytes();
        masks.insert(id, std::move(mask));
    }
    return masks;
}

std::optional<QHash<QString, PackedMaskRegion>> binaryMaskTableFromJson(
    const QJsonValue &value, quint64 &remainingAssetBytes, QString *error)
{
    if (!value.isArray())
    {
        setError(error,
            DocumentSerializer::tr(
                "The project contains an invalid binary mask table."));
        return std::nullopt;
    }
    const QJsonArray entries = value.toArray();
    if (entries.size() > DocumentLimits::maximumTotalStrokes)
    {
        setError(error,
            DocumentSerializer::tr(
                "The project contains too many binary masks."));
        return std::nullopt;
    }
    QHash<QString, PackedMaskRegion> masks;
    masks.reserve(entries.size());
    for (const QJsonValue &entryValue : entries)
    {
        if (!entryValue.isObject())
        {
            setError(error,
                DocumentSerializer::tr(
                    "The project contains an invalid binary mask table."));
            return std::nullopt;
        }
        const QJsonObject entry = entryValue.toObject();
        const QString id = entry.value(QStringLiteral("id")).toString();
        const QJsonArray canvas =
            entry.value(QStringLiteral("canvas")).toArray();
        const QJsonArray bounds =
            entry.value(QStringLiteral("bounds")).toArray();
        if (!entry.value(QStringLiteral("id")).isString()
            || !isValidMaskContentId(id)
            || !entry.value(QStringLiteral("canvas")).isArray()
            || canvas.size() != 2
            || !entry.value(QStringLiteral("bounds")).isArray()
            || bounds.size() != 4
            || !entry.value(QStringLiteral("data")).isString()
            || masks.contains(id))
        {
            setError(error,
                DocumentSerializer::tr(
                    "The project contains an invalid binary mask table."));
            return std::nullopt;
        }
        const std::optional<int> canvasWidth = integerFromJson(canvas[0]);
        const std::optional<int> canvasHeight = integerFromJson(canvas[1]);
        const std::optional<int> x = integerFromJson(bounds[0]);
        const std::optional<int> y = integerFromJson(bounds[1]);
        const std::optional<int> width = integerFromJson(bounds[2]);
        const std::optional<int> height = integerFromJson(bounds[3]);
        if (!canvasWidth || !canvasHeight || !x || !y || !width || !height
            || *canvasWidth < DocumentLimits::minimumCanvasEdge
            || *canvasHeight < DocumentLimits::minimumCanvasEdge
            || *canvasWidth > DocumentLimits::maximumCanvasEdge
            || *canvasHeight > DocumentLimits::maximumCanvasEdge || *x < 0
            || *y < 0 || *width <= 0 || *height <= 0 || *width > *canvasWidth
            || *height > *canvasHeight || *x > *canvasWidth - *width
            || *y > *canvasHeight - *height)
        {
            setError(error,
                DocumentSerializer::tr(
                    "The project contains an invalid binary mask table."));
            return std::nullopt;
        }
        PackedMaskRegion region;
        region.canvasSize = QSize(*canvasWidth, *canvasHeight);
        region.bounds = QRect(*x, *y, *width, *height);
        const qsizetype stride = (static_cast<qsizetype>(*width) + 7) / 8;
        if (*width <= 0 || *height <= 0 || stride <= 0
            || stride > std::numeric_limits<qsizetype>::max() / *height)
        {
            setError(error,
                DocumentSerializer::tr(
                    "The project contains an invalid binary mask."));
            return std::nullopt;
        }
        const quint64 expectedBytes =
            static_cast<quint64>(stride) * static_cast<quint64>(*height);
        const QByteArray compressed = QByteArray::fromBase64(
            entry.value(QStringLiteral("data")).toString().toLatin1());
        if (compressed.size() < 4)
        {
            setError(error,
                DocumentSerializer::tr(
                    "The project contains an invalid binary mask."));
            return std::nullopt;
        }
        const auto *header =
            reinterpret_cast<const uchar *>(compressed.constData());
        const quint32 declaredSize = (static_cast<quint32>(header[0]) << 24U)
                                     | (static_cast<quint32>(header[1]) << 16U)
                                     | (static_cast<quint32>(header[2]) << 8U)
                                     | static_cast<quint32>(header[3]);
        if (expectedBytes > std::numeric_limits<quint32>::max()
            || declaredSize != expectedBytes
            || expectedBytes > remainingAssetBytes)
        {
            setError(error,
                DocumentSerializer::tr(
                    "The project contains too much binary mask data."));
            return std::nullopt;
        }
        region.packedMask = qUncompress(compressed);
        if (static_cast<quint64>(region.packedMask.size()) != expectedBytes
            || !isValidPackedMaskRegion(region)
            || binaryMaskContentId(region) != id)
        {
            setError(error,
                DocumentSerializer::tr(
                    "The project contains an invalid binary mask."));
            return std::nullopt;
        }
        remainingAssetBytes -= expectedBytes;
        masks.insert(id, std::move(region));
    }
    return masks;
}

std::optional<SamplingMode> samplingModeFromJson(const QJsonValue &value)
{
    if (!value.isString())
    {
        return std::nullopt;
    }
    const QString name = value.toString();
    if (name == QStringLiteral("nearest"))
    {
        return SamplingMode::Nearest;
    }
    if (name == QStringLiteral("smooth"))
    {
        return SamplingMode::Smooth;
    }
    return std::nullopt;
}

std::optional<LayerBlendMode> layerBlendModeFromJson(const QJsonValue &value)
{
    if (!value.isString())
    {
        return std::nullopt;
    }
    const QString name = value.toString();
    if (name == QStringLiteral("normal"))
    {
        return LayerBlendMode::Normal;
    }
    if (name == QStringLiteral("multiply"))
    {
        return LayerBlendMode::Multiply;
    }
    if (name == QStringLiteral("screen"))
    {
        return LayerBlendMode::Screen;
    }
    if (name == QStringLiteral("overlay"))
    {
        return LayerBlendMode::Overlay;
    }
    return std::nullopt;
}

std::optional<LayerKind> layerKindFromJson(const QJsonValue &value)
{
    if (!value.isString())
    {
        return std::nullopt;
    }
    const QString name = value.toString();
    if (name == QStringLiteral("paint"))
    {
        return LayerKind::Paint;
    }
    if (name == QStringLiteral("group"))
    {
        return LayerKind::Group;
    }
    return std::nullopt;
}

std::optional<QSize> sizeFromJsonArray(const QJsonValue &value)
{
    if (!value.isArray())
    {
        return std::nullopt;
    }
    const QJsonArray values = value.toArray();
    if (values.size() != 2)
    {
        return std::nullopt;
    }
    const std::optional<int> width = integerFromJson(values[0]);
    const std::optional<int> height = integerFromJson(values[1]);
    if (!width || !height)
    {
        return std::nullopt;
    }
    return QSize(*width, *height);
}

std::optional<QTransform> transformFromJson(const QJsonValue &value)
{
    if (!value.isArray())
    {
        return std::nullopt;
    }
    const QJsonArray values = value.toArray();
    if (values.size() != 9
        || !std::all_of(values.cbegin(),
            values.cend(),
            [](const QJsonValue &entry)
            {
                return entry.isDouble() && std::isfinite(entry.toDouble());
            }))
    {
        return std::nullopt;
    }
    return QTransform(values[0].toDouble(),
        values[1].toDouble(),
        values[2].toDouble(),
        values[3].toDouble(),
        values[4].toDouble(),
        values[5].toDouble(),
        values[6].toDouble(),
        values[7].toDouble(),
        values[8].toDouble());
}

std::optional<Stroke> strokeFromJson(const QJsonValue &value,
    int fileSchemaVersion,
    const QSize &canvasSize,
    QHash<QByteArray, QImage> &maskCache,
    const QHash<QString, QImage> &referencedMasks,
    const QHash<QString, PackedMaskRegion> &referencedBinaryMasks,
    quint64 &distinctMaskBytes,
    QString *error)
{
    if (!value.isObject())
    {
        setError(
            error, DocumentSerializer::tr("A stroke entry is not an object."));
        return std::nullopt;
    }

    const QJsonObject object = value.toObject();
    if (!object.value(QStringLiteral("id")).isString()
        || !object.value(QStringLiteral("seed")).isString()
        || !object.value(QStringLiteral("mode")).isString()
        || !object.value(QStringLiteral("color")).isString()
        || !object.value(QStringLiteral("width")).isDouble()
        || !object.value(QStringLiteral("points")).isArray())
    {
        setError(
            error, DocumentSerializer::tr("A stroke contains invalid fields."));
        return std::nullopt;
    }
    const QJsonArray points = object.value(QStringLiteral("points")).toArray();
    if (points.size() > DocumentLimits::maximumPointsPerStroke)
    {
        setError(error,
            DocumentSerializer::tr("A stroke has an invalid point count."));
        return std::nullopt;
    }

    Stroke stroke;
    const QUuid id(object.value(QStringLiteral("id")).toString());
    if (id.isNull())
    {
        setError(error, DocumentSerializer::tr("A stroke has an invalid ID."));
        return std::nullopt;
    }
    stroke.id = id;
    const QString seedText = object.value(QStringLiteral("seed")).toString();
    if (seedText.isEmpty()
        || !std::all_of(seedText.cbegin(),
            seedText.cend(),
            [](QChar character)
            {
                return character >= QLatin1Char('0')
                       && character <= QLatin1Char('9');
            }))
    {
        setError(
            error, DocumentSerializer::tr("A stroke has an invalid seed."));
        return std::nullopt;
    }
    bool seedValid = false;
    stroke.seed = seedText.toULongLong(&seedValid);
    if (!seedValid)
    {
        setError(
            error, DocumentSerializer::tr("A stroke has an invalid seed."));
        return std::nullopt;
    }
    const QString mode = object.value(QStringLiteral("mode")).toString();
    if (mode != QStringLiteral("paint") && mode != QStringLiteral("erase")
        && mode != QStringLiteral("fill")
        && (fileSchemaVersion < 6
            || (mode != QStringLiteral("pixelSelection")
                && mode != QStringLiteral("reframe"))))
    {
        setError(
            error, DocumentSerializer::tr("A stroke has an invalid mode."));
        return std::nullopt;
    }
    if (mode == QStringLiteral("erase"))
    {
        stroke.mode = StrokeMode::Erase;
    }
    else if (mode == QStringLiteral("fill"))
    {
        stroke.mode = StrokeMode::Fill;
    }
    else if (mode == QStringLiteral("pixelSelection"))
    {
        stroke.mode = StrokeMode::PixelSelection;
    }
    else if (mode == QStringLiteral("reframe"))
    {
        stroke.mode = StrokeMode::Reframe;
    }
    else
    {
        stroke.mode = StrokeMode::Paint;
    }
    const QColor color(object.value(QStringLiteral("color")).toString());
    if (!color.isValid())
    {
        setError(
            error, DocumentSerializer::tr("A stroke has an invalid color."));
        return std::nullopt;
    }
    stroke.color = color;
    stroke.width = object.value(QStringLiteral("width")).toDouble();
    if (!std::isfinite(stroke.width)
        || stroke.width < DocumentLimits::minimumStrokeWidth
        || stroke.width > DocumentLimits::maximumStrokeWidth)
    {
        setError(
            error, DocumentSerializer::tr("A stroke has an invalid width."));
        return std::nullopt;
    }
    if (fileSchemaVersion >= 2)
    {
        const std::optional<BrushSettings> brush =
            brushFromJson(object.value(QStringLiteral("brush")), error);
        if (!brush)
        {
            return std::nullopt;
        }
        stroke.brush = *brush;
    }
    stroke.points.reserve(points.size());

    for (const QJsonValue &pointValue : points)
    {
        const std::optional<StrokePoint> point = pointFromJson(pointValue);
        if (!point)
        {
            setError(error,
                DocumentSerializer::tr("A stroke contains an invalid point."));
            return std::nullopt;
        }
        stroke.points.append(*point);
    }
    const bool framebufferOperation = stroke.mode == StrokeMode::PixelSelection
                                      || stroke.mode == StrokeMode::Reframe;
    if (framebufferOperation != points.isEmpty())
    {
        setError(error,
            DocumentSerializer::tr("A stroke has an invalid point count."));
        return std::nullopt;
    }
    if (fileSchemaVersion >= 4)
    {
        if (object.contains(QStringLiteral("clipMask")))
        {
            setError(error,
                DocumentSerializer::tr("A stroke contains a legacy clip "
                                       "mask in a current project."));
            return std::nullopt;
        }
        const QJsonValue clipMaskId =
            object.value(QStringLiteral("clipMaskId"));
        if (!clipMaskId.isUndefined())
        {
            if (!clipMaskId.isString())
            {
                setError(error,
                    DocumentSerializer::tr(
                        "A stroke has an invalid clip mask reference."));
                return std::nullopt;
            }
            const auto mask = referencedMasks.constFind(clipMaskId.toString());
            if (mask == referencedMasks.cend())
            {
                setError(error,
                    DocumentSerializer::tr(
                        "A stroke references a missing clip mask."));
                return std::nullopt;
            }
            stroke.clipMask = mask.value();
        }
        if (fileSchemaVersion >= 5)
        {
            const QJsonValue visibility =
                object.value(QStringLiteral("visibilityClip"));
            if (!visibility.isUndefined())
            {
                if (!visibility.isArray())
                {
                    setError(error,
                        DocumentSerializer::tr(
                            "A stroke has an invalid visibility clip."));
                    return std::nullopt;
                }
                const QJsonArray values = visibility.toArray();
                if (values.size() != 4)
                {
                    setError(error,
                        DocumentSerializer::tr(
                            "A stroke has an invalid visibility clip."));
                    return std::nullopt;
                }
                const std::optional<int> x = integerFromJson(values[0]);
                const std::optional<int> y = integerFromJson(values[1]);
                const std::optional<int> width = integerFromJson(values[2]);
                const std::optional<int> height = integerFromJson(values[3]);
                const int maximumWidth =
                    fileSchemaVersion <= 5 ? canvasSize.width()
                                           : DocumentLimits::maximumCanvasEdge;
                const int maximumHeight =
                    fileSchemaVersion <= 5 ? canvasSize.height()
                                           : DocumentLimits::maximumCanvasEdge;
                const bool validBounds =
                    x && y && width && height && *x >= 0 && *y >= 0
                    && *width > 0 && *height > 0 && *width <= maximumWidth
                    && *height <= maximumHeight && *x <= maximumWidth - *width
                    && *y <= maximumHeight - *height;
                if (!validBounds)
                {
                    setError(error,
                        DocumentSerializer::tr(
                            "A stroke has an invalid visibility clip."));
                    return std::nullopt;
                }
                const QRect rect(*x, *y, *width, *height);
                stroke.visibilityClip = rect;
            }

            const QJsonValue fillMaskId =
                object.value(QStringLiteral("fillMaskId"));
            if (!fillMaskId.isUndefined())
            {
                if (stroke.mode != StrokeMode::Fill || !fillMaskId.isString())
                {
                    setError(error,
                        DocumentSerializer::tr(
                            "A stroke has an invalid fill mask reference."));
                    return std::nullopt;
                }
                const auto mask =
                    referencedMasks.constFind(fillMaskId.toString());
                if (mask == referencedMasks.cend())
                {
                    setError(error,
                        DocumentSerializer::tr(
                            "A stroke references a missing fill mask."));
                    return std::nullopt;
                }
                stroke.fillMask = mask.value();
            }
            if (fileSchemaVersion >= 6)
            {
                if (object.contains(QStringLiteral("fillCoverageId")))
                {
                    setError(error,
                        DocumentSerializer::tr(
                            "A stroke references unsupported fill coverage."));
                    return std::nullopt;
                }
                const QJsonValue pixelSelectionPayload =
                    object.value(QStringLiteral("pixelSelection"));
                const QJsonValue reframePayload =
                    object.value(QStringLiteral("reframe"));
                if ((stroke.mode != StrokeMode::PixelSelection
                        && !pixelSelectionPayload.isUndefined())
                    || (stroke.mode != StrokeMode::Reframe
                        && !reframePayload.isUndefined()))
                {
                    setError(error,
                        DocumentSerializer::tr("A stroke contains an operation "
                                               "payload for the wrong mode."));
                    return std::nullopt;
                }

                if (stroke.mode == StrokeMode::PixelSelection)
                {
                    const QJsonValue payloadValue = pixelSelectionPayload;
                    if (!payloadValue.isObject())
                    {
                        setError(error,
                            DocumentSerializer::tr(
                                "A pixel selection operation is invalid."));
                        return std::nullopt;
                    }
                    const QJsonObject payload = payloadValue.toObject();
                    const QString maskId =
                        payload.value(QStringLiteral("maskId")).toString();
                    const auto mask = referencedBinaryMasks.constFind(maskId);
                    const std::optional<QTransform> operationTransform =
                        transformFromJson(
                            payload.value(QStringLiteral("transform")));
                    const std::optional<SamplingMode> sampling =
                        samplingModeFromJson(
                            payload.value(QStringLiteral("sampling")));
                    if (!payload.value(QStringLiteral("maskId")).isString()
                        || mask == referencedBinaryMasks.cend()
                        || !operationTransform || !sampling
                        || !payload.value(QStringLiteral("clearSource"))
                            .isBool()
                        || !payload.value(QStringLiteral("drawDestination"))
                            .isBool())
                    {
                        setError(error,
                            DocumentSerializer::tr(
                                "A pixel selection operation is invalid."));
                        return std::nullopt;
                    }
                    PixelSelectionOp operation;
                    operation.canvasSize = mask->canvasSize;
                    operation.sourceBounds = mask->bounds;
                    operation.packedMask = mask->packedMask;
                    operation.transform = *operationTransform;
                    operation.sampling = *sampling;
                    operation.clearSource =
                        payload.value(QStringLiteral("clearSource")).toBool();
                    operation.drawDestination =
                        payload.value(QStringLiteral("drawDestination"))
                            .toBool();
                    if (!isValidPixelSelectionOp(operation))
                    {
                        setError(error,
                            DocumentSerializer::tr(
                                "A pixel selection operation is invalid."));
                        return std::nullopt;
                    }
                    stroke.pixelSelectionOp = std::move(operation);
                }

                if (stroke.mode == StrokeMode::Reframe)
                {
                    const QJsonValue payloadValue = reframePayload;
                    if (!payloadValue.isObject())
                    {
                        setError(error,
                            DocumentSerializer::tr(
                                "A reframe operation is invalid."));
                        return std::nullopt;
                    }
                    const QJsonObject payload = payloadValue.toObject();
                    const QString reframeMode =
                        payload.value(QStringLiteral("mode")).toString();
                    const std::optional<SamplingMode> sampling =
                        samplingModeFromJson(
                            payload.value(QStringLiteral("sampling")));
                    const std::optional<QSize> sourceSize = sizeFromJsonArray(
                        payload.value(QStringLiteral("sourceSize")));
                    const std::optional<QSize> targetSize = sizeFromJsonArray(
                        payload.value(QStringLiteral("targetSize")));
                    const QJsonValue offsetValue =
                        payload.value(QStringLiteral("contentOffset"));
                    const QJsonArray offset = offsetValue.toArray();
                    const std::optional<int> offsetX =
                        offset.size() == 2 ? integerFromJson(offset[0])
                                           : std::nullopt;
                    const std::optional<int> offsetY =
                        offset.size() == 2 ? integerFromJson(offset[1])
                                           : std::nullopt;
                    if (!payload.value(QStringLiteral("mode")).isString()
                        || (reframeMode != QStringLiteral("canvas")
                            && reframeMode != QStringLiteral("image"))
                        || !sampling || !sourceSize || !targetSize
                        || !offsetValue.isArray() || !offsetX || !offsetY)
                    {
                        setError(error,
                            DocumentSerializer::tr(
                                "A reframe operation is invalid."));
                        return std::nullopt;
                    }
                    ReframeOp operation;
                    operation.mode = reframeMode == QStringLiteral("image")
                                         ? ReframeMode::Image
                                         : ReframeMode::Canvas;
                    operation.sampling = *sampling;
                    operation.sourceSize = *sourceSize;
                    operation.targetSize = *targetSize;
                    operation.contentOffset = QPoint(*offsetX, *offsetY);
                    if (!isValidReframeOp(operation))
                    {
                        setError(error,
                            DocumentSerializer::tr(
                                "A reframe operation is invalid."));
                        return std::nullopt;
                    }
                    stroke.reframeOp = operation;
                }
            }
        }
    }
    else if (fileSchemaVersion >= 3
             && object.contains(QStringLiteral("clipMask")))
    {
        const std::optional<QImage> clipMask =
            legacyClipMaskFromJson(object.value(QStringLiteral("clipMask")),
                maskCache,
                distinctMaskBytes,
                error);
        if (!clipMask)
        {
            return std::nullopt;
        }
        stroke.clipMask = *clipMask;
    }
    return stroke;
}

QJsonObject layerToJson(const Layer &layer,
    const ClipMaskTable &clipMasks,
    const BinaryMaskTable &binaryMasks)
{
    QJsonArray strokes;
    for (const Stroke &stroke : layer.strokes)
    {
        strokes.append(strokeToJson(stroke, clipMasks, binaryMasks));
    }

    QJsonObject object;
    object.insert(
        QStringLiteral("id"), layer.id.toString(QUuid::WithoutBraces));
    object.insert(QStringLiteral("name"), layer.name);
    object.insert(QStringLiteral("kind"), layerKindName(layer.kind));
    object.insert(QStringLiteral("parentGroupId"),
        layer.parentGroupId.isNull()
            ? QJsonValue(QJsonValue::Null)
            : QJsonValue(layer.parentGroupId.toString(QUuid::WithoutBraces)));
    object.insert(QStringLiteral("clipToLayerBelow"), layer.clipToLayerBelow);
    object.insert(QStringLiteral("visible"), layer.visible);
    object.insert(QStringLiteral("reference"), layer.reference);
    object.insert(QStringLiteral("opacity"), layer.opacity);
    object.insert(
        QStringLiteral("blendMode"), layerBlendModeName(layer.blendMode));
    object.insert(QStringLiteral("initialCanvasSize"),
        QJsonArray{
            layer.initialCanvasSize.width(), layer.initialCanvasSize.height()});
    object.insert(QStringLiteral("strokes"), strokes);
    return object;
}

QJsonObject layerSkeletonToJson(const Layer &layer)
{
    QJsonObject object;
    object.insert(
        QStringLiteral("id"), layer.id.toString(QUuid::WithoutBraces));
    object.insert(QStringLiteral("name"), layer.name);
    object.insert(QStringLiteral("kind"), layerKindName(layer.kind));
    object.insert(QStringLiteral("parentGroupId"),
        layer.parentGroupId.isNull()
            ? QJsonValue(QJsonValue::Null)
            : QJsonValue(layer.parentGroupId.toString(QUuid::WithoutBraces)));
    object.insert(QStringLiteral("clipToLayerBelow"), layer.clipToLayerBelow);
    object.insert(QStringLiteral("visible"), layer.visible);
    object.insert(QStringLiteral("reference"), layer.reference);
    object.insert(QStringLiteral("opacity"), layer.opacity);
    object.insert(
        QStringLiteral("blendMode"), layerBlendModeName(layer.blendMode));
    object.insert(QStringLiteral("initialCanvasSize"),
        QJsonArray{
            layer.initialCanvasSize.width(), layer.initialCanvasSize.height()});
    object.insert(QStringLiteral("strokes"), QJsonArray());
    return object;
}

QJsonObject rootToJson(const Document &document,
    const QJsonArray &layers,
    const QJsonArray &clipMasks,
    const QJsonArray &binaryMasks,
    const QJsonObject &additionalRootFields = {})
{
    QJsonObject canvas;
    canvas.insert(QStringLiteral("width"), document.size.width());
    canvas.insert(QStringLiteral("height"), document.size.height());
    canvas.insert(QStringLiteral("background"),
        document.background.name(QColor::HexArgb));

    QJsonObject animation;
    animation.insert(QStringLiteral("frames"), document.animationFrames);
    animation.insert(QStringLiteral("fps"), document.framesPerSecond);
    animation.insert(QStringLiteral("wobble"), document.wobbleAmount);

    QJsonObject root;
    root.insert(QStringLiteral("schemaVersion"), schemaVersion);
    root.insert(QStringLiteral("algorithmVersion"), algorithmVersion);
    root.insert(QStringLiteral("canvas"), canvas);
    root.insert(QStringLiteral("animation"), animation);
    root.insert(QStringLiteral("activeLayerId"),
        document.activeLayerId.isNull()
            ? QJsonValue(QJsonValue::Null)
            : QJsonValue(
                  document.activeLayerId.toString(QUuid::WithoutBraces)));
    root.insert(QStringLiteral("layers"), layers);
    root.insert(QStringLiteral("clipMasks"), clipMasks);
    root.insert(QStringLiteral("binaryMasks"), binaryMasks);
    for (auto field = additionalRootFields.constBegin();
        field != additionalRootFields.constEnd();
        ++field)
    {
        root.insert(field.key(), field.value());
    }
    return root;
}

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

std::optional<Layer> layerFromJson(const QJsonValue &value,
    int fileSchemaVersion,
    const QSize &canvasSize,
    QHash<QByteArray, QImage> &maskCache,
    const QHash<QString, QImage> &referencedMasks,
    const QHash<QString, PackedMaskRegion> &referencedBinaryMasks,
    quint64 &distinctMaskBytes,
    QString *error)
{
    if (!value.isObject())
    {
        setError(
            error, DocumentSerializer::tr("A layer entry is not an object."));
        return std::nullopt;
    }

    const QJsonObject object = value.toObject();
    const QJsonValue referenceValue = object.value(QStringLiteral("reference"));
    if (!object.value(QStringLiteral("id")).isString()
        || !object.value(QStringLiteral("name")).isString()
        || !object.value(QStringLiteral("visible")).isBool()
        || !object.value(QStringLiteral("opacity")).isDouble()
        || !object.value(QStringLiteral("strokes")).isArray()
        || (fileSchemaVersion >= 9 && !referenceValue.isBool())
        || (fileSchemaVersion >= 8
            && (!object.value(QStringLiteral("kind")).isString()
                || (!object.value(QStringLiteral("parentGroupId")).isString()
                    && !object.value(QStringLiteral("parentGroupId")).isNull())
                || !object.value(QStringLiteral("clipToLayerBelow")).isBool()))
        || (fileSchemaVersion >= 7
            && !object.value(QStringLiteral("blendMode")).isString())
        || (fileSchemaVersion >= 6
            && !object.value(QStringLiteral("initialCanvasSize")).isArray()))
    {
        setError(
            error, DocumentSerializer::tr("A layer contains invalid fields."));
        return std::nullopt;
    }
    const QJsonArray strokes =
        object.value(QStringLiteral("strokes")).toArray();
    if (strokes.size() > DocumentLimits::maximumStrokesPerLayer)
    {
        setError(error,
            DocumentSerializer::tr("A layer contains too many strokes."));
        return std::nullopt;
    }

    Layer layer;
    const QUuid id(object.value(QStringLiteral("id")).toString());
    if (id.isNull())
    {
        setError(error, DocumentSerializer::tr("A layer has an invalid ID."));
        return std::nullopt;
    }
    layer.id = id;
    layer.name = object.value(QStringLiteral("name")).toString();
    if (layer.name.trimmed().isEmpty()
        || layer.name.size() > DocumentLimits::maximumLayerNameLength)
    {
        setError(error, DocumentSerializer::tr("A layer has an invalid name."));
        return std::nullopt;
    }
    if (fileSchemaVersion >= 8)
    {
        const std::optional<LayerKind> kind =
            layerKindFromJson(object.value(QStringLiteral("kind")));
        const QJsonValue parentValue =
            object.value(QStringLiteral("parentGroupId"));
        const QUuid parentId =
            parentValue.isNull() ? QUuid() : QUuid(parentValue.toString());
        if (!kind || (!parentValue.isNull() && parentId.isNull()))
        {
            setError(error,
                DocumentSerializer::tr(
                    "A layer has invalid hierarchy fields."));
            return std::nullopt;
        }
        layer.kind = *kind;
        layer.parentGroupId = parentId;
        layer.clipToLayerBelow =
            object.value(QStringLiteral("clipToLayerBelow")).toBool();
    }
    layer.visible = object.value(QStringLiteral("visible")).toBool();
    if (fileSchemaVersion >= 9)
    {
        layer.reference = referenceValue.toBool();
    }
    layer.opacity = object.value(QStringLiteral("opacity")).toDouble();
    if (!std::isfinite(layer.opacity) || layer.opacity < 0.0
        || layer.opacity > 1.0)
    {
        setError(
            error, DocumentSerializer::tr("A layer has an invalid opacity."));
        return std::nullopt;
    }
    if (fileSchemaVersion >= 7)
    {
        const std::optional<LayerBlendMode> blendMode =
            layerBlendModeFromJson(object.value(QStringLiteral("blendMode")));
        if (!blendMode)
        {
            setError(error,
                DocumentSerializer::tr("A layer has an invalid blend mode."));
            return std::nullopt;
        }
        layer.blendMode = *blendMode;
    }
    layer.initialCanvasSize = canvasSize;
    if (fileSchemaVersion >= 6)
    {
        const std::optional<QSize> initialSize = sizeFromJsonArray(
            object.value(QStringLiteral("initialCanvasSize")));
        if (!initialSize
            || initialSize->width() < DocumentLimits::minimumCanvasEdge
            || initialSize->height() < DocumentLimits::minimumCanvasEdge
            || initialSize->width() > DocumentLimits::maximumCanvasEdge
            || initialSize->height() > DocumentLimits::maximumCanvasEdge)
        {
            setError(error,
                DocumentSerializer::tr(
                    "A layer has an invalid initial canvas size."));
            return std::nullopt;
        }
        layer.initialCanvasSize = *initialSize;
    }
    layer.strokes.reserve(strokes.size());

    for (const QJsonValue &strokeValue : strokes)
    {
        const std::optional<Stroke> stroke = strokeFromJson(strokeValue,
            fileSchemaVersion,
            canvasSize,
            maskCache,
            referencedMasks,
            referencedBinaryMasks,
            distinctMaskBytes,
            error);
        if (!stroke)
        {
            return std::nullopt;
        }
        layer.strokes.append(*stroke);
    }
    return layer;
}

std::optional<int> integerFromJson(const QJsonValue &value)
{
    if (!value.isDouble())
    {
        return std::nullopt;
    }
    const double number = value.toDouble();
    if (!std::isfinite(number) || std::trunc(number) != number
        || number < static_cast<double>(std::numeric_limits<int>::min())
        || number > static_cast<double>(std::numeric_limits<int>::max()))
    {
        return std::nullopt;
    }
    return static_cast<int>(number);
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
