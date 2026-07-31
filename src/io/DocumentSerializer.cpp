#include "io/DocumentSerializer.hpp"

#include "document/DocumentLimits.hpp"

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

namespace wobble {

namespace {

constexpr int schemaVersion = 4;
constexpr int algorithmVersion = 2;

std::optional<int> integerFromJson(const QJsonValue &value);

void setError(QString *error, const QString &message)
{
    if (error) {
        *error = message;
    }
}

QByteArray canonicalMaskBytes(const QImage &mask)
{
    if (mask.isNull()
        || mask.format() != QImage::Format_Grayscale8) {
        return {};
    }
    const qint64 byteCount =
        static_cast<qint64>(mask.width()) * mask.height();
    if (byteCount <= 0
        || byteCount > std::numeric_limits<int>::max()) {
        return {};
    }
    QByteArray bytes(static_cast<qsizetype>(byteCount), '\0');
    for (int y = 0; y < mask.height(); ++y) {
        std::memcpy(
            bytes.data()
                + static_cast<qsizetype>(y) * mask.width(),
            mask.constScanLine(y),
            static_cast<std::size_t>(mask.width()));
    }
    return bytes;
}

QString maskContentId(
    int width,
    int height,
    const QByteArray &canonicalBytes)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(QByteArray::number(width));
    hash.addData(QByteArrayLiteral("x"));
    hash.addData(QByteArray::number(height));
    hash.addData(QByteArrayLiteral(":"));
    hash.addData(canonicalBytes);
    return QString::fromLatin1(hash.result().toHex());
}

struct SerializedClipMask {
    QString id;
    QImage image;
    QString encodedData;
};

struct ClipMaskTable {
    QHash<qint64, QString> idByCacheKey;
    QMap<QString, SerializedClipMask> entries;
    qint64 serializedEntryBytes = 0;
    bool tooLarge = false;
    bool invalid = false;
};

QJsonObject serializedClipMaskToJson(
    const SerializedClipMask &entry)
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), entry.id);
    object.insert(QStringLiteral("width"), entry.image.width());
    object.insert(QStringLiteral("height"), entry.image.height());
    object.insert(QStringLiteral("data"), entry.encodedData);
    return object;
}

QString registerClipMask(
    const QImage &mask,
    ClipMaskTable &table,
    qint64 maximumBytes)
{
    if (mask.isNull()) {
        return {};
    }
    const qint64 cacheKey = mask.cacheKey();
    const auto cachedId = table.idByCacheKey.constFind(cacheKey);
    if (cachedId != table.idByCacheKey.cend()) {
        return cachedId.value();
    }

    const QByteArray bytes = canonicalMaskBytes(mask);
    if (bytes.isEmpty()) {
        table.invalid = true;
        return {};
    }
    const QString id =
        maskContentId(mask.width(), mask.height(), bytes);
    const auto existing = table.entries.constFind(id);
    if (existing != table.entries.cend()) {
        if (existing->image.size() != mask.size()
            || existing->image != mask) {
            table.invalid = true;
            return {};
        }
        table.idByCacheKey.insert(cacheKey, id);
        return id;
    }

    SerializedClipMask entry;
    entry.id = id;
    entry.image = mask;
    entry.encodedData =
        QString::fromLatin1(qCompress(bytes, 6).toBase64());
    const qint64 entryBytes =
        QJsonDocument(serializedClipMaskToJson(entry))
            .toJson(QJsonDocument::Compact)
            .size();
    if (entryBytes > maximumBytes
        || table.serializedEntryBytes
            > maximumBytes - entryBytes) {
        table.tooLarge = true;
        return {};
    }
    table.serializedEntryBytes += entryBytes;
    table.idByCacheKey.insert(cacheKey, id);
    table.entries.insert(id, std::move(entry));
    return id;
}

ClipMaskTable buildClipMaskTable(
    const Document &document,
    qint64 maximumBytes)
{
    ClipMaskTable table;
    for (const Layer &layer : document.layers) {
        for (const Stroke &stroke : layer.strokes) {
            if (stroke.clipMask.isNull()) {
                continue;
            }
            if (registerClipMask(
                    stroke.clipMask,
                    table,
                    maximumBytes).isEmpty()) {
                return table;
            }
        }
    }
    return table;
}

QJsonArray pointToJson(const StrokePoint &point)
{
    return {
        point.position.x(),
        point.position.y(),
        point.pressure
    };
}

std::optional<StrokePoint> pointFromJson(const QJsonValue &value)
{
    if (!value.isArray()) {
        return std::nullopt;
    }
    const QJsonArray array = value.toArray();
    if ((array.size() != 2 && array.size() != 3)
        || !array[0].isDouble() || !array[1].isDouble()
        || (array.size() == 3 && !array[2].isDouble())) {
        return std::nullopt;
    }
    StrokePoint point;
    point.position = QPointF(array[0].toDouble(), array[1].toDouble());
    point.pressure = array.size() == 3
        ? array[2].toDouble()
        : 1.0;
    if (!std::isfinite(point.position.x())
        || !std::isfinite(point.position.y())
        || !std::isfinite(point.pressure)
        || point.pressure < 0.0
        || point.pressure > 1.0) {
        return std::nullopt;
    }
    return point;
}

QJsonObject brushToJson(const BrushSettings &brush)
{
    QString engineName = QStringLiteral("line");
    if (brush.engine == BrushEngine::Airbrush) {
        engineName = QStringLiteral("airbrush");
    } else if (brush.engine == BrushEngine::Spray) {
        engineName = QStringLiteral("spray");
    }
    const QString tipName =
        brush.tipShape == BrushTipShape::Square
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
    const QJsonValue &value,
    QString *error)
{
    if (!value.isObject()) {
        setError(error, DocumentSerializer::tr("A stroke has invalid brush settings."));
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
        || !object.value(QStringLiteral("animatedJitter")).isBool()) {
        setError(error, DocumentSerializer::tr("A stroke has invalid brush settings."));
        return std::nullopt;
    }
    const QJsonValue wobbleScaleValue =
        object.value(QStringLiteral("wobbleScale"));
    if (!wobbleScaleValue.isUndefined() && !wobbleScaleValue.isDouble()) {
        setError(error, DocumentSerializer::tr("A stroke has invalid brush settings."));
        return std::nullopt;
    }
    const QJsonValue antialiasingValue =
        object.value(QStringLiteral("antialiasing"));
    if (!antialiasingValue.isUndefined() && !antialiasingValue.isBool()) {
        setError(error, DocumentSerializer::tr("A stroke has invalid brush settings."));
        return std::nullopt;
    }

    BrushSettings brush;
    const QString engine = object.value(QStringLiteral("engine")).toString();
    if (engine == QStringLiteral("airbrush")) {
        brush.engine = BrushEngine::Airbrush;
    } else if (engine == QStringLiteral("spray")) {
        brush.engine = BrushEngine::Spray;
    } else if (engine != QStringLiteral("line")) {
        setError(error, DocumentSerializer::tr("A stroke has an invalid brush engine."));
        return std::nullopt;
    }
    const QString tip = object.value(QStringLiteral("tip")).toString();
    if (tip == QStringLiteral("square")) {
        brush.tipShape = BrushTipShape::Square;
    } else if (tip != QStringLiteral("round")) {
        setError(error, DocumentSerializer::tr("A stroke has an invalid brush tip."));
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
    brush.sizeJitter =
        object.value(QStringLiteral("sizeJitter")).toDouble();
    brush.animatedJitter =
        object.value(QStringLiteral("animatedJitter")).toBool();
    brush.wobbleScale =
        wobbleScaleValue.isDouble() ? wobbleScaleValue.toDouble() : 1.0;
    brush.antialiasing = antialiasingValue.toBool();
    if (!isValidBrushSettings(brush)) {
        setError(error, DocumentSerializer::tr("A stroke has invalid brush settings."));
        return std::nullopt;
    }
    return brush;
}

QJsonObject strokeToJson(
    const Stroke &stroke,
    const ClipMaskTable &clipMasks)
{
    QJsonArray points;
    for (const StrokePoint &point : stroke.points) {
        points.append(pointToJson(point));
    }

    QJsonObject object;
    object.insert(QStringLiteral("id"), stroke.id.toString(QUuid::WithoutBraces));
    object.insert(QStringLiteral("seed"), QString::number(stroke.seed));
    QString modeName = QStringLiteral("paint");
    if (stroke.mode == StrokeMode::Erase) {
        modeName = QStringLiteral("erase");
    } else if (stroke.mode == StrokeMode::Fill) {
        modeName = QStringLiteral("fill");
    }
    object.insert(QStringLiteral("mode"), modeName);
    object.insert(
        QStringLiteral("color"),
        stroke.color.name(QColor::HexArgb));
    object.insert(QStringLiteral("width"), stroke.width);
    object.insert(QStringLiteral("brush"), brushToJson(stroke.brush));
    object.insert(QStringLiteral("points"), points);
    if (!stroke.clipMask.isNull()) {
        const auto id =
            clipMasks.idByCacheKey.constFind(stroke.clipMask.cacheKey());
        if (id != clipMasks.idByCacheKey.cend()) {
            object.insert(QStringLiteral("clipMaskId"), id.value());
        }
    }
    return object;
}

std::optional<QImage> legacyClipMaskFromJson(
    const QJsonValue &value,
    QHash<QByteArray, QImage> &maskCache,
    quint64 &distinctMaskBytes,
    QString *error)
{
    if (!value.isObject()) {
        setError(error, DocumentSerializer::tr("A stroke has an invalid clip mask."));
        return std::nullopt;
    }
    const QJsonObject object = value.toObject();
    const std::optional<int> width =
        integerFromJson(object.value(QStringLiteral("width")));
    const std::optional<int> height =
        integerFromJson(object.value(QStringLiteral("height")));
    if (!width
        || !height
        || !object.value(QStringLiteral("data")).isString()
        || *width < DocumentLimits::minimumCanvasEdge
        || *height < DocumentLimits::minimumCanvasEdge
        || *width > DocumentLimits::maximumCanvasEdge
        || *height > DocumentLimits::maximumCanvasEdge) {
        setError(error, DocumentSerializer::tr("A stroke has an invalid clip mask."));
        return std::nullopt;
    }

    const QByteArray compressed = QByteArray::fromBase64(
        object.value(QStringLiteral("data")).toString().toLatin1());
    if (compressed.size() < 4) {
        setError(error, DocumentSerializer::tr("A stroke has an invalid clip mask."));
        return std::nullopt;
    }
    const auto *header =
        reinterpret_cast<const uchar *>(compressed.constData());
    const quint32 declaredSize =
        (static_cast<quint32>(header[0]) << 24U)
        | (static_cast<quint32>(header[1]) << 16U)
        | (static_cast<quint32>(header[2]) << 8U)
        | static_cast<quint32>(header[3]);
    const quint64 expectedSize =
        static_cast<quint64>((*width + 3) & ~3)
        * static_cast<quint64>(*height);
    if (expectedSize > std::numeric_limits<quint32>::max()
        || declaredSize != expectedSize) {
        setError(error, DocumentSerializer::tr("A stroke has an invalid clip mask."));
        return std::nullopt;
    }
    QByteArray cacheKey =
        QByteArray::number(*width)
        + 'x'
        + QByteArray::number(*height)
        + ':'
        + compressed;
    const auto cached = maskCache.constFind(cacheKey);
    if (cached != maskCache.cend()) {
        return cached.value();
    }
    if (declaredSize
        > DocumentLimits::maximumDistinctClipMaskBytes
            - distinctMaskBytes) {
        setError(error, DocumentSerializer::tr("The project contains too much selection data."));
        return std::nullopt;
    }
    QImage mask(QSize(*width, *height), QImage::Format_Grayscale8);
    if (mask.isNull()
        || static_cast<quint64>(mask.sizeInBytes()) != expectedSize) {
        setError(error, DocumentSerializer::tr("A stroke clip mask is too large."));
        return std::nullopt;
    }
    const QByteArray bytes = qUncompress(compressed);
    if (bytes.size() != mask.sizeInBytes()) {
        setError(error, DocumentSerializer::tr("A stroke has an invalid clip mask."));
        return std::nullopt;
    }
    std::memcpy(
        mask.bits(),
        bytes.constData(),
        static_cast<std::size_t>(bytes.size()));
    for (int y = 0; y < mask.height(); ++y) {
        std::fill(
            mask.scanLine(y) + mask.width(),
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
        && std::all_of(
            id.cbegin(),
            id.cend(),
            [](QChar character) {
                return (character >= QLatin1Char('0')
                        && character <= QLatin1Char('9'))
                    || (character >= QLatin1Char('a')
                        && character <= QLatin1Char('f'));
            });
}

std::optional<QHash<QString, QImage>> clipMaskTableFromJson(
    const QJsonValue &value,
    const QSize &canvasSize,
    QString *error)
{
    if (!value.isArray()) {
        setError(
            error,
            DocumentSerializer::tr(
                "The project contains an invalid selection mask table."));
        return std::nullopt;
    }
    const QJsonArray entries = value.toArray();
    if (entries.size() > DocumentLimits::maximumTotalStrokes) {
        setError(
            error,
            DocumentSerializer::tr(
                "The project contains too many selection masks."));
        return std::nullopt;
    }

    QHash<QString, QImage> masks;
    masks.reserve(entries.size());
    quint64 distinctMaskBytes = 0;
    for (const QJsonValue &entryValue : entries) {
        if (!entryValue.isObject()) {
            setError(
                error,
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
            || !isValidMaskContentId(id)
            || !width
            || !height
            || QSize(*width, *height) != canvasSize
            || !entry.value(QStringLiteral("data")).isString()
            || masks.contains(id)) {
            setError(
                error,
                DocumentSerializer::tr(
                    "The project contains an invalid selection mask table."));
            return std::nullopt;
        }

        const QByteArray compressed = QByteArray::fromBase64(
            entry.value(QStringLiteral("data")).toString().toLatin1());
        if (compressed.size() < 4) {
            setError(
                error,
                DocumentSerializer::tr(
                    "The project contains an invalid selection mask."));
            return std::nullopt;
        }
        const auto *header =
            reinterpret_cast<const uchar *>(compressed.constData());
        const quint32 declaredSize =
            (static_cast<quint32>(header[0]) << 24U)
            | (static_cast<quint32>(header[1]) << 16U)
            | (static_cast<quint32>(header[2]) << 8U)
            | static_cast<quint32>(header[3]);
        const quint64 canonicalSize =
            static_cast<quint64>(*width)
            * static_cast<quint64>(*height);
        const quint64 paddedSize =
            static_cast<quint64>((*width + 3) & ~3)
            * static_cast<quint64>(*height);
        if (canonicalSize > std::numeric_limits<quint32>::max()
            || declaredSize != canonicalSize) {
            setError(
                error,
                DocumentSerializer::tr(
                    "The project contains an invalid selection mask."));
            return std::nullopt;
        }

        if (paddedSize
            > DocumentLimits::maximumDistinctClipMaskBytes
                - distinctMaskBytes) {
            setError(
                error,
                DocumentSerializer::tr(
                    "The project contains too much selection data."));
            return std::nullopt;
        }
        QImage mask(canvasSize, QImage::Format_Grayscale8);
        if (mask.isNull()
            || static_cast<quint64>(mask.sizeInBytes()) != paddedSize) {
            setError(
                error,
                DocumentSerializer::tr(
                    "The project contains an invalid selection mask."));
            return std::nullopt;
        }
        const QByteArray bytes = qUncompress(compressed);
        if (static_cast<quint64>(bytes.size()) != canonicalSize
            || maskContentId(*width, *height, bytes) != id) {
            setError(
                error,
                DocumentSerializer::tr(
                    "The project contains an invalid selection mask."));
            return std::nullopt;
        }
        mask.fill(0);
        for (int y = 0; y < *height; ++y) {
            std::memcpy(
                mask.scanLine(y),
                bytes.constData()
                    + static_cast<qsizetype>(y) * *width,
                static_cast<std::size_t>(*width));
        }
        distinctMaskBytes += mask.sizeInBytes();
        masks.insert(id, std::move(mask));
    }
    return masks;
}

std::optional<Stroke> strokeFromJson(
    const QJsonValue &value,
    int fileSchemaVersion,
    QHash<QByteArray, QImage> &maskCache,
    const QHash<QString, QImage> &referencedMasks,
    quint64 &distinctMaskBytes,
    QString *error)
{
    if (!value.isObject()) {
        setError(error, DocumentSerializer::tr("A stroke entry is not an object."));
        return std::nullopt;
    }

    const QJsonObject object = value.toObject();
    if (!object.value(QStringLiteral("id")).isString()
        || !object.value(QStringLiteral("seed")).isString()
        || !object.value(QStringLiteral("mode")).isString()
        || !object.value(QStringLiteral("color")).isString()
        || !object.value(QStringLiteral("width")).isDouble()
        || !object.value(QStringLiteral("points")).isArray()) {
        setError(error, DocumentSerializer::tr("A stroke contains invalid fields."));
        return std::nullopt;
    }
    const QJsonArray points = object.value(QStringLiteral("points")).toArray();
    if (points.isEmpty()
        || points.size() > DocumentLimits::maximumPointsPerStroke) {
        setError(error, DocumentSerializer::tr("A stroke has an invalid point count."));
        return std::nullopt;
    }

    Stroke stroke;
    const QUuid id(object.value(QStringLiteral("id")).toString());
    if (id.isNull()) {
        setError(error, DocumentSerializer::tr("A stroke has an invalid ID."));
        return std::nullopt;
    }
    stroke.id = id;
    const QString seedText =
        object.value(QStringLiteral("seed")).toString();
    if (seedText.isEmpty()
        || !std::all_of(
            seedText.cbegin(),
            seedText.cend(),
            [](QChar character) {
                return character >= QLatin1Char('0')
                    && character <= QLatin1Char('9');
            })) {
        setError(error, DocumentSerializer::tr("A stroke has an invalid seed."));
        return std::nullopt;
    }
    bool seedValid = false;
    stroke.seed = seedText.toULongLong(&seedValid);
    if (!seedValid) {
        setError(error, DocumentSerializer::tr("A stroke has an invalid seed."));
        return std::nullopt;
    }
    const QString mode = object.value(QStringLiteral("mode")).toString();
    if (mode != QStringLiteral("paint")
        && mode != QStringLiteral("erase")
        && mode != QStringLiteral("fill")) {
        setError(error, DocumentSerializer::tr("A stroke has an invalid mode."));
        return std::nullopt;
    }
    if (mode == QStringLiteral("erase")) {
        stroke.mode = StrokeMode::Erase;
    } else if (mode == QStringLiteral("fill")) {
        stroke.mode = StrokeMode::Fill;
    } else {
        stroke.mode = StrokeMode::Paint;
    }
    const QColor color(object.value(QStringLiteral("color")).toString());
    if (!color.isValid()) {
        setError(error, DocumentSerializer::tr("A stroke has an invalid color."));
        return std::nullopt;
    }
    stroke.color = color;
    stroke.width = object.value(QStringLiteral("width")).toDouble();
    if (!std::isfinite(stroke.width)
        || stroke.width < DocumentLimits::minimumStrokeWidth
        || stroke.width > DocumentLimits::maximumStrokeWidth) {
        setError(error, DocumentSerializer::tr("A stroke has an invalid width."));
        return std::nullopt;
    }
    if (fileSchemaVersion >= 2) {
        const std::optional<BrushSettings> brush =
            brushFromJson(object.value(QStringLiteral("brush")), error);
        if (!brush) {
            return std::nullopt;
        }
        stroke.brush = *brush;
    }
    stroke.points.reserve(points.size());

    for (const QJsonValue &pointValue : points) {
        const std::optional<StrokePoint> point = pointFromJson(pointValue);
        if (!point) {
            setError(error, DocumentSerializer::tr("A stroke contains an invalid point."));
            return std::nullopt;
        }
        stroke.points.append(*point);
    }
    if (fileSchemaVersion >= 4) {
        if (object.contains(QStringLiteral("clipMask"))) {
            setError(
                error,
                DocumentSerializer::tr(
                    "A stroke contains a legacy clip mask in a current project."));
            return std::nullopt;
        }
        const QJsonValue clipMaskId =
            object.value(QStringLiteral("clipMaskId"));
        if (!clipMaskId.isUndefined()) {
            if (!clipMaskId.isString()) {
                setError(
                    error,
                    DocumentSerializer::tr(
                        "A stroke has an invalid clip mask reference."));
                return std::nullopt;
            }
            const auto mask =
                referencedMasks.constFind(clipMaskId.toString());
            if (mask == referencedMasks.cend()) {
                setError(
                    error,
                    DocumentSerializer::tr(
                        "A stroke references a missing clip mask."));
                return std::nullopt;
            }
            stroke.clipMask = mask.value();
        }
    } else if (fileSchemaVersion >= 3
        && object.contains(QStringLiteral("clipMask"))) {
        const std::optional<QImage> clipMask = legacyClipMaskFromJson(
            object.value(QStringLiteral("clipMask")),
            maskCache,
            distinctMaskBytes,
            error);
        if (!clipMask) {
            return std::nullopt;
        }
        stroke.clipMask = *clipMask;
    }
    return stroke;
}

QJsonObject layerToJson(
    const Layer &layer,
    const ClipMaskTable &clipMasks)
{
    QJsonArray strokes;
    for (const Stroke &stroke : layer.strokes) {
        strokes.append(strokeToJson(stroke, clipMasks));
    }

    QJsonObject object;
    object.insert(QStringLiteral("id"), layer.id.toString(QUuid::WithoutBraces));
    object.insert(QStringLiteral("name"), layer.name);
    object.insert(QStringLiteral("visible"), layer.visible);
    object.insert(QStringLiteral("opacity"), layer.opacity);
    object.insert(QStringLiteral("strokes"), strokes);
    return object;
}

QJsonObject layerSkeletonToJson(const Layer &layer)
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), layer.id.toString(QUuid::WithoutBraces));
    object.insert(QStringLiteral("name"), layer.name);
    object.insert(QStringLiteral("visible"), layer.visible);
    object.insert(QStringLiteral("opacity"), layer.opacity);
    object.insert(QStringLiteral("strokes"), QJsonArray());
    return object;
}

QJsonObject rootToJson(
    const Document &document,
    const QJsonArray &layers,
    const QJsonArray &clipMasks)
{
    QJsonObject canvas;
    canvas.insert(QStringLiteral("width"), document.size.width());
    canvas.insert(QStringLiteral("height"), document.size.height());
    canvas.insert(
        QStringLiteral("background"),
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
    root.insert(
        QStringLiteral("activeLayerId"),
        document.activeLayerId.isNull()
            ? QJsonValue(QJsonValue::Null)
            : QJsonValue(
                  document.activeLayerId.toString(QUuid::WithoutBraces)));
    root.insert(QStringLiteral("layers"), layers);
    root.insert(QStringLiteral("clipMasks"), clipMasks);
    return root;
}

bool addSerializedBytes(
    qint64 &total,
    qint64 amount,
    qint64 maximumBytes)
{
    if (amount < 0
        || amount > maximumBytes
        || total > maximumBytes - amount) {
        return false;
    }
    total += amount;
    return true;
}

bool serializedSizeWithinLimit(
    const Document &document,
    const ClipMaskTable &clipMasks,
    qint64 maximumBytes)
{
    qint64 serializedLayerBytes = 0;
    for (const Layer &layer : document.layers) {
        qint64 serializedStrokeBytes = 0;
        for (const Stroke &stroke : layer.strokes) {
            const qint64 bytes =
                QJsonDocument(strokeToJson(stroke, clipMasks))
                    .toJson(QJsonDocument::Compact)
                    .size();
            if (!addSerializedBytes(
                    serializedStrokeBytes,
                    bytes,
                    maximumBytes)) {
                return false;
            }
        }
        const qint64 strokeArrayBytes =
            2
            + serializedStrokeBytes
            + std::max<qint64>(0, layer.strokes.size() - 1);
        const qint64 skeletonBytes =
            QJsonDocument(layerSkeletonToJson(layer))
                .toJson(QJsonDocument::Compact)
                .size();
        const qint64 layerBytes =
            skeletonBytes - 2 + strokeArrayBytes;
        if (!addSerializedBytes(
                serializedLayerBytes,
                layerBytes,
                maximumBytes)) {
            return false;
        }
    }

    const qint64 layerArrayBytes =
        2
        + serializedLayerBytes
        + std::max<qint64>(0, document.layers.size() - 1);
    const qint64 maskArrayBytes =
        2
        + clipMasks.serializedEntryBytes
        + std::max<qint64>(0, clipMasks.entries.size() - 1);
    const qint64 rootSkeletonBytes =
        QJsonDocument(rootToJson(document, {}, {}))
            .toJson(QJsonDocument::Compact)
            .size();
    qint64 total = rootSkeletonBytes - 4;
    return addSerializedBytes(total, layerArrayBytes, maximumBytes)
        && addSerializedBytes(total, maskArrayBytes, maximumBytes);
}

std::optional<QByteArray> serializeDocument(
    const Document &document,
    qint64 maximumBytes)
{
    const ClipMaskTable clipMasks =
        buildClipMaskTable(document, maximumBytes);
    if (clipMasks.invalid
        || clipMasks.tooLarge
        || !serializedSizeWithinLimit(
            document,
            clipMasks,
            maximumBytes)) {
        return std::nullopt;
    }

    QJsonArray layers;
    for (const Layer &layer : document.layers) {
        layers.append(layerToJson(layer, clipMasks));
    }
    QJsonArray masks;
    for (const SerializedClipMask &entry : clipMasks.entries) {
        masks.append(serializedClipMaskToJson(entry));
    }
    QByteArray data =
        QJsonDocument(rootToJson(document, layers, masks))
            .toJson(QJsonDocument::Compact);
    if (data.size() > maximumBytes) {
        return std::nullopt;
    }
    return data;
}

std::optional<Layer> layerFromJson(
    const QJsonValue &value,
    int fileSchemaVersion,
    QHash<QByteArray, QImage> &maskCache,
    const QHash<QString, QImage> &referencedMasks,
    quint64 &distinctMaskBytes,
    QString *error)
{
    if (!value.isObject()) {
        setError(error, DocumentSerializer::tr("A layer entry is not an object."));
        return std::nullopt;
    }

    const QJsonObject object = value.toObject();
    if (!object.value(QStringLiteral("id")).isString()
        || !object.value(QStringLiteral("name")).isString()
        || !object.value(QStringLiteral("visible")).isBool()
        || !object.value(QStringLiteral("opacity")).isDouble()
        || !object.value(QStringLiteral("strokes")).isArray()) {
        setError(error, DocumentSerializer::tr("A layer contains invalid fields."));
        return std::nullopt;
    }
    const QJsonArray strokes = object.value(QStringLiteral("strokes")).toArray();
    if (strokes.size() > DocumentLimits::maximumStrokesPerLayer) {
        setError(error, DocumentSerializer::tr("A layer contains too many strokes."));
        return std::nullopt;
    }

    Layer layer;
    const QUuid id(object.value(QStringLiteral("id")).toString());
    if (id.isNull()) {
        setError(error, DocumentSerializer::tr("A layer has an invalid ID."));
        return std::nullopt;
    }
    layer.id = id;
    layer.name = object.value(QStringLiteral("name")).toString();
    if (layer.name.trimmed().isEmpty()
        || layer.name.size() > DocumentLimits::maximumLayerNameLength) {
        setError(error, DocumentSerializer::tr("A layer has an invalid name."));
        return std::nullopt;
    }
    layer.visible = object.value(QStringLiteral("visible")).toBool();
    layer.opacity = object.value(QStringLiteral("opacity")).toDouble();
    if (!std::isfinite(layer.opacity)
        || layer.opacity < 0.0 || layer.opacity > 1.0) {
        setError(error, DocumentSerializer::tr("A layer has an invalid opacity."));
        return std::nullopt;
    }
    layer.strokes.reserve(strokes.size());

    for (const QJsonValue &strokeValue : strokes) {
        const std::optional<Stroke> stroke =
            strokeFromJson(
                strokeValue,
                fileSchemaVersion,
                maskCache,
                referencedMasks,
                distinctMaskBytes,
                error);
        if (!stroke) {
            return std::nullopt;
        }
        layer.strokes.append(*stroke);
    }
    return layer;
}

std::optional<int> integerFromJson(const QJsonValue &value)
{
    if (!value.isDouble()) {
        return std::nullopt;
    }
    const double number = value.toDouble();
    if (!std::isfinite(number)
        || std::trunc(number) != number
        || number < static_cast<double>(std::numeric_limits<int>::min())
        || number > static_cast<double>(std::numeric_limits<int>::max())) {
        return std::nullopt;
    }
    return static_cast<int>(number);
}

bool validateCollectionBudgets(
    const QJsonArray &layers,
    QString *error)
{
    qsizetype remainingStrokes = DocumentLimits::maximumTotalStrokes;
    qsizetype remainingPoints = DocumentLimits::maximumTotalPoints;

    for (const QJsonValue &layerValue : layers) {
        if (!layerValue.isObject()) {
            continue;
        }
        const QJsonValue strokesValue =
            layerValue.toObject().value(QStringLiteral("strokes"));
        if (!strokesValue.isArray()) {
            continue;
        }
        const QJsonArray strokes = strokesValue.toArray();
        if (strokes.size() > DocumentLimits::maximumStrokesPerLayer
            || strokes.size() > remainingStrokes) {
            setError(error, DocumentSerializer::tr("The project contains too many strokes."));
            return false;
        }
        remainingStrokes -= strokes.size();

        for (const QJsonValue &strokeValue : strokes) {
            if (!strokeValue.isObject()) {
                continue;
            }
            const QJsonValue pointsValue =
                strokeValue.toObject().value(QStringLiteral("points"));
            if (!pointsValue.isArray()) {
                continue;
            }
            const qsizetype pointCount = pointsValue.toArray().size();
            if (pointCount > DocumentLimits::maximumPointsPerStroke
                || pointCount > remainingPoints) {
                setError(error, DocumentSerializer::tr("The project contains too many points."));
                return false;
            }
            remainingPoints -= pointCount;
        }
    }
    return true;
}

bool validateDocument(const Document &document, QString *error)
{
    if (document.size.width() < DocumentLimits::minimumCanvasEdge
        || document.size.height() < DocumentLimits::minimumCanvasEdge
        || document.size.width() > DocumentLimits::maximumCanvasEdge
        || document.size.height() > DocumentLimits::maximumCanvasEdge) {
        setError(error, DocumentSerializer::tr("The canvas size is invalid."));
        return false;
    }
    if (!document.background.isValid()) {
        setError(error, DocumentSerializer::tr("The canvas background is invalid."));
        return false;
    }
    if (document.animationFrames < DocumentLimits::minimumAnimationFrames
        || document.animationFrames > DocumentLimits::maximumAnimationFrames
        || !std::isfinite(document.framesPerSecond)
        || document.framesPerSecond < DocumentLimits::minimumFramesPerSecond
        || document.framesPerSecond > DocumentLimits::maximumFramesPerSecond
        || !std::isfinite(document.wobbleAmount)
        || document.wobbleAmount < DocumentLimits::minimumWobbleAmount
        || document.wobbleAmount > DocumentLimits::maximumWobbleAmount) {
        setError(error, DocumentSerializer::tr("The animation settings are invalid."));
        return false;
    }
    if (document.layers.size() > DocumentLimits::maximumLayers) {
        setError(error, DocumentSerializer::tr("The layer count is invalid."));
        return false;
    }

    QSet<QUuid> layerIds;
    QSet<QUuid> strokeIds;
    QSet<qint64> clipMaskKeys;
    qsizetype totalStrokes = 0;
    qsizetype totalPoints = 0;
    quint64 clipMaskBytes = 0;
    bool activeLayerFound = false;
    for (const Layer &layer : document.layers) {
        if (layer.id.isNull() || layerIds.contains(layer.id)) {
            setError(error, DocumentSerializer::tr("The project contains invalid layer IDs."));
            return false;
        }
        layerIds.insert(layer.id);
        activeLayerFound = activeLayerFound || layer.id == document.activeLayerId;
        if (layer.name.trimmed().isEmpty()
            || layer.name.size() > DocumentLimits::maximumLayerNameLength
            || !std::isfinite(layer.opacity)
            || layer.opacity < 0.0
            || layer.opacity > 1.0
            || layer.strokes.size() > DocumentLimits::maximumStrokesPerLayer
            || layer.strokes.size()
                > DocumentLimits::maximumTotalStrokes - totalStrokes) {
            setError(error, DocumentSerializer::tr("A layer contains invalid data."));
            return false;
        }
        totalStrokes += layer.strokes.size();

        for (const Stroke &stroke : layer.strokes) {
            if (stroke.id.isNull() || strokeIds.contains(stroke.id)) {
                setError(error, DocumentSerializer::tr("The project contains invalid stroke IDs."));
                return false;
            }
            strokeIds.insert(stroke.id);
            if (!stroke.clipMask.isNull()
                && !clipMaskKeys.contains(stroke.clipMask.cacheKey())) {
                clipMaskKeys.insert(stroke.clipMask.cacheKey());
                const quint64 maskBytes = stroke.clipMask.sizeInBytes();
                if (maskBytes
                    > DocumentLimits::maximumDistinctClipMaskBytes
                        - clipMaskBytes) {
                    setError(
                        error,
                        QStringLiteral(
                            "The project contains too much selection data."));
                    return false;
                }
                clipMaskBytes += maskBytes;
            }
            if ((stroke.mode != StrokeMode::Paint
                 && stroke.mode != StrokeMode::Erase
                 && stroke.mode != StrokeMode::Fill)
                || !stroke.color.isValid()
                || !std::isfinite(stroke.width)
                || stroke.width < DocumentLimits::minimumStrokeWidth
                || stroke.width > DocumentLimits::maximumStrokeWidth
                || !isValidBrushSettings(stroke.brush)
                || stroke.points.isEmpty()
                || stroke.points.size() > DocumentLimits::maximumPointsPerStroke
                || (!stroke.clipMask.isNull()
                    && (stroke.clipMask.size() != document.size
                        || stroke.clipMask.format()
                            != QImage::Format_Grayscale8))
                || stroke.points.size()
                    > DocumentLimits::maximumTotalPoints - totalPoints) {
                setError(error, DocumentSerializer::tr("A stroke contains invalid data."));
                return false;
            }
            totalPoints += stroke.points.size();
            for (const StrokePoint &point : stroke.points) {
                if (!std::isfinite(point.position.x())
                    || !std::isfinite(point.position.y())
                    || !std::isfinite(point.pressure)
                    || point.position.x() < 0.0
                    || point.position.y() < 0.0
                    || point.position.x() > document.size.width()
                    || point.position.y() > document.size.height()
                    || point.pressure < 0.0
                    || point.pressure > 1.0) {
                    setError(error, DocumentSerializer::tr("A stroke contains an invalid point."));
                    return false;
                }
            }
        }
    }
    const bool validActiveLayer =
        document.layers.isEmpty()
        ? document.activeLayerId.isNull()
        : !document.activeLayerId.isNull() && activeLayerFound;
    if (!validActiveLayer) {
        setError(error, DocumentSerializer::tr("The active layer ID is invalid."));
        return false;
    }
    return true;
}

}

bool DocumentSerializer::save(
    const QString &filePath,
    const Document &document,
    QString *error)
{
    if (!validateDocument(document, error)) {
        return false;
    }
    const std::optional<QByteArray> data = serializeDocument(
        document,
        DocumentLimits::maximumProjectBytes);
    if (!data) {
        setError(error, DocumentSerializer::tr("The project is too large to save."));
        return false;
    }
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        setError(error, file.errorString());
        return false;
    }
    if (file.write(*data) != data->size()) {
        setError(error, file.errorString());
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        setError(error, file.errorString());
        return false;
    }
    return true;
}

std::optional<Document> DocumentSerializer::load(
    const QString &filePath,
    QString *error)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error, file.errorString());
        return std::nullopt;
    }
    if (file.size() < 0
        || file.size() > DocumentLimits::maximumProjectBytes) {
        setError(error, DocumentSerializer::tr("The project file is too large."));
        return std::nullopt;
    }
    return fromJson(file.readAll(), error);
}

QByteArray DocumentSerializer::toJson(const Document &document)
{
    const std::optional<QByteArray> data = serializeDocument(
        document,
        DocumentLimits::maximumProjectBytes);
    return data.value_or(QByteArray());
}

std::optional<Document> DocumentSerializer::fromJson(
    const QByteArray &data,
    QString *error)
{
    if (data.size() > DocumentLimits::maximumProjectBytes) {
        setError(error, DocumentSerializer::tr("The project data is too large."));
        return std::nullopt;
    }
    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(data, &parseError);
    if (json.isNull() || !json.isObject()) {
        setError(error, parseError.errorString());
        return std::nullopt;
    }

    const QJsonObject root = json.object();
    const std::optional<int> fileSchemaVersion =
        integerFromJson(root.value(QStringLiteral("schemaVersion")));
    if (!fileSchemaVersion
        || *fileSchemaVersion < 1
        || *fileSchemaVersion > schemaVersion) {
        setError(error, DocumentSerializer::tr("This project version is not supported."));
        return std::nullopt;
    }
    const std::optional<int> fileAlgorithmVersion =
        integerFromJson(root.value(QStringLiteral("algorithmVersion")));
    if (!fileAlgorithmVersion
        || *fileAlgorithmVersion < 1
        || *fileAlgorithmVersion > algorithmVersion) {
        setError(
            error,
            DocumentSerializer::tr("This rendering algorithm version is not supported."));
        return std::nullopt;
    }

    const QJsonValue activeLayerIdValue =
        root.value(QStringLiteral("activeLayerId"));
    if (!root.value(QStringLiteral("canvas")).isObject()
        || !root.value(QStringLiteral("animation")).isObject()
        || (!activeLayerIdValue.isString()
            && !activeLayerIdValue.isNull())
        || !root.value(QStringLiteral("layers")).isArray()
        || (*fileSchemaVersion >= 4
            && !root.value(QStringLiteral("clipMasks")).isArray())) {
        setError(error, DocumentSerializer::tr("The project contains invalid fields."));
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
        || *height > DocumentLimits::maximumCanvasEdge) {
        setError(error, DocumentSerializer::tr("The canvas size is invalid."));
        return std::nullopt;
    }
    const QColor background(
        canvas.value(QStringLiteral("background")).toString());
    if (!background.isValid()) {
        setError(error, DocumentSerializer::tr("The canvas background is invalid."));
        return std::nullopt;
    }
    const QJsonArray layers = root.value(QStringLiteral("layers")).toArray();
    if (layers.size() > DocumentLimits::maximumLayers) {
        setError(error, DocumentSerializer::tr("The layer count is invalid."));
        return std::nullopt;
    }
    if ((layers.isEmpty() && !activeLayerIdValue.isNull())
        || (!layers.isEmpty() && !activeLayerIdValue.isString())) {
        setError(error, DocumentSerializer::tr("The active layer ID is invalid."));
        return std::nullopt;
    }
    if (!validateCollectionBudgets(layers, error)) {
        return std::nullopt;
    }

    QHash<QString, QImage> referencedMasks;
    if (*fileSchemaVersion >= 4) {
        const std::optional<QHash<QString, QImage>> parsedMasks =
            clipMaskTableFromJson(
                root.value(QStringLiteral("clipMasks")),
                QSize(*width, *height),
                error);
        if (!parsedMasks) {
            return std::nullopt;
        }
        referencedMasks = *parsedMasks;
    }

    const QJsonObject animation =
        root.value(QStringLiteral("animation")).toObject();
    const std::optional<int> frames =
        integerFromJson(animation.value(QStringLiteral("frames")));
    if (!frames
        || !animation.value(QStringLiteral("fps")).isDouble()
        || !animation.value(QStringLiteral("wobble")).isDouble()) {
        setError(error, DocumentSerializer::tr("The animation settings are invalid."));
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
        || wobbleAmount > DocumentLimits::maximumWobbleAmount) {
        setError(error, DocumentSerializer::tr("The animation settings are invalid."));
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

    for (const QJsonValue &layerValue : layers) {
        const std::optional<Layer> layer =
            layerFromJson(
                layerValue,
                *fileSchemaVersion,
                maskCache,
                referencedMasks,
                distinctMaskBytes,
                error);
        if (!layer) {
            return std::nullopt;
        }
        if (layerIds.contains(layer->id)) {
            setError(error, DocumentSerializer::tr("The project contains duplicate layer IDs."));
            return std::nullopt;
        }
        layerIds.insert(layer->id);
        for (const Stroke &stroke : layer->strokes) {
            if (stroke.points.size()
                > DocumentLimits::maximumTotalPoints - totalPoints) {
                setError(error, DocumentSerializer::tr("The project contains too many points."));
                return std::nullopt;
            }
            totalPoints += stroke.points.size();
            if (strokeIds.contains(stroke.id)) {
                setError(
                    error,
                    DocumentSerializer::tr("The project contains duplicate stroke IDs."));
                return std::nullopt;
            }
            strokeIds.insert(stroke.id);
        }
        document.layers.append(*layer);
    }

    document.activeLayerId = activeLayerIdValue.isNull()
        ? QUuid()
        : QUuid(activeLayerIdValue.toString());
    if (!validateDocument(document, error)) {
        return std::nullopt;
    }
    return document;
}

}
