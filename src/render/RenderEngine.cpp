#include "render/RenderEngine.hpp"

#include "document/DocumentLimits.hpp"

#include <QHash>
#include <QPainter>
#include <QRadialGradient>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace wobble {

namespace {

constexpr qsizetype maximumResampledPoints =
    DocumentLimits::maximumPointsPerStroke;
constexpr qsizetype maximumBrushDabs = 50000;
constexpr qsizetype maximumSprayParticles = 250000;

quint64 mixHash(quint64 value)
{
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

qreal signedNoise(quint64 seed, int frame, int index, quint64 channel)
{
    quint64 value = seed;
    value ^= mixHash(static_cast<quint64>(frame + 1) * 0x517cc1b727220a95ULL);
    value ^= mixHash(static_cast<quint64>(index + 4099) * 0x6eed0e9da4d94a4fULL);
    value ^= channel;
    const quint64 result = mixHash(value);
    const qreal unit = static_cast<qreal>(result >> 11U)
        / static_cast<qreal>(1ULL << 53U);
    return unit * 2.0 - 1.0;
}

qreal smoothFieldNoise(
    quint64 seed,
    int frame,
    qreal coordinate,
    quint64 channel)
{
    const int left = static_cast<int>(std::floor(coordinate));
    const qreal fraction = coordinate - static_cast<qreal>(left);
    const qreal blend = fraction * fraction * (3.0 - 2.0 * fraction);
    const qreal a = signedNoise(seed, frame, left, channel);
    const qreal b = signedNoise(seed, frame, left + 1, channel);
    return a + (b - a) * blend;
}

QVector<StrokePoint> resample(
    const QVector<StrokePoint> &source,
    qreal spacing,
    qsizetype maximumPoints = maximumResampledPoints)
{
    if (!std::isfinite(spacing)
        || spacing <= 0.0
        || maximumPoints < 2
        || maximumPoints > maximumResampledPoints
        || source.size() > DocumentLimits::maximumPointsPerStroke) {
        return {};
    }
    if (source.size() < 2) {
        if (source.isEmpty()) {
            return {};
        }
        const StrokePoint &point = source.first();
        if (!std::isfinite(point.position.x())
            || !std::isfinite(point.position.y())
            || !std::isfinite(point.pressure)
            || point.position.x() < 0.0
            || point.position.y() < 0.0
            || point.position.x() > DocumentLimits::maximumCanvasEdge
            || point.position.y() > DocumentLimits::maximumCanvasEdge
            || point.pressure < 0.0
            || point.pressure > 1.0) {
            return {};
        }
        return source;
    }

    long double totalLength = 0.0L;
    for (int index = 0; index < source.size(); ++index) {
        const StrokePoint &point = source[index];
        if (!std::isfinite(point.position.x())
            || !std::isfinite(point.position.y())
            || !std::isfinite(point.pressure)
            || point.position.x() < 0.0
            || point.position.y() < 0.0
            || point.position.x() > DocumentLimits::maximumCanvasEdge
            || point.position.y() > DocumentLimits::maximumCanvasEdge
            || point.pressure < 0.0
            || point.pressure > 1.0) {
            return {};
        }
        if (index > 0) {
            const long double x =
                static_cast<long double>(point.position.x())
                - static_cast<long double>(source[index - 1].position.x());
            const long double y =
                static_cast<long double>(point.position.y())
                - static_cast<long double>(source[index - 1].position.y());
            totalLength += std::hypot(x, y);
        }
    }

    const long double minimumSpacing =
        totalLength / static_cast<long double>(maximumPoints - 1);
    spacing = std::max(
        spacing,
        static_cast<qreal>(minimumSpacing));

    QVector<StrokePoint> result;
    result.reserve(std::min(
        maximumPoints,
        source.size() * 2));
    result.append(source.first());

    StrokePoint previous = source.first();
    qreal distanceToNext = spacing;

    for (int index = 1; index < source.size(); ++index) {
        const StrokePoint target = source[index];
        QPointF delta = target.position - previous.position;
        qreal segmentLength = std::hypot(delta.x(), delta.y());

        while (segmentLength >= distanceToNext && segmentLength > 0.0) {
            if (result.size() >= maximumPoints - 1) {
                result.append(source.last());
                return result;
            }
            const qreal ratio = distanceToNext / segmentLength;
            StrokePoint point;
            point.position = previous.position + delta * ratio;
            point.pressure = previous.pressure
                + (target.pressure - previous.pressure) * ratio;
            result.append(point);
            previous = point;
            delta = target.position - previous.position;
            segmentLength = std::hypot(delta.x(), delta.y());
            distanceToNext = spacing;
        }

        distanceToNext -= segmentLength;
        previous = target;
    }

    const QPointF tail = source.last().position - result.last().position;
    if (std::hypot(tail.x(), tail.y()) > 0.01
        && result.size() < maximumPoints) {
        result.append(source.last());
    }
    return result;
}

QPointF normalized(const QPointF &value)
{
    const qreal length = std::hypot(value.x(), value.y());
    if (length <= 0.00001) {
        return QPointF(1.0, 0.0);
    }
    return value / length;
}

QVector<StrokePoint> displacedStrokePoints(
    const Stroke &stroke,
    int frameIndex,
    int frameCount,
    qreal wobbleAmount,
    qreal requestedSpacing = -1.0,
    qsizetype maximumPoints = maximumResampledPoints)
{
    if (stroke.points.isEmpty()) {
        return {};
    }

    const int normalizedCount = std::max(1, frameCount);
    const int normalizedFrame =
        ((frameIndex % normalizedCount) + normalizedCount) % normalizedCount;
    const qreal spacing = requestedSpacing > 0.0
        ? requestedSpacing
        : std::clamp(stroke.width * 0.55, 2.0, 5.0);
    QVector<StrokePoint> samples =
        resample(stroke.points, spacing, maximumPoints);
    QVector<QPointF> basePositions;
    basePositions.reserve(samples.size());
    for (const StrokePoint &sample : samples) {
        basePositions.append(sample.position);
    }
    const qreal amplitude =
        std::max(0.0, wobbleAmount)
        * (0.82 + std::min(stroke.width, 40.0) * 0.018);
    constexpr qreal swayWavelength = 26.0;
    constexpr qreal detailWavelength = 9.0;

    qreal arcLength = 0.0;
    for (int index = 0; index < samples.size(); ++index) {
        const QPointF before = basePositions[std::max(0, index - 1)];
        const QPointF after = basePositions[
            std::min(static_cast<int>(basePositions.size()) - 1, index + 1)];
        if (index > 0) {
            const QPointF step =
                basePositions[index] - basePositions[index - 1];
            arcLength += std::hypot(step.x(), step.y());
        }
        const QPointF tangent = normalized(after - before);
        const QPointF normal(-tangent.y(), tangent.x());
        const qreal sway = smoothFieldNoise(
            stroke.seed,
            normalizedFrame,
            arcLength / swayWavelength,
            0xb5297a4dULL);
        const qreal detail = smoothFieldNoise(
            stroke.seed,
            normalizedFrame,
            arcLength / detailWavelength + 31.0,
            0x1b56c4e9ULL);
        const qreal normalOffset = sway * 0.72 + detail * 0.28;
        const qreal tangentOffset = smoothFieldNoise(
            stroke.seed,
            normalizedFrame,
            arcLength / swayWavelength + 57.0,
            0x68e31da4ULL);
        const qreal pressureScale = 0.8 + samples[index].pressure * 0.2;
        samples[index].position +=
            normal * normalOffset * amplitude * pressureScale
            + tangent * tangentOffset * amplitude * 0.3;
    }
    return samples;
}

QPainterPath smoothedPath(const QVector<QPointF> &points)
{
    QPainterPath path;
    if (points.isEmpty()) {
        return path;
    }
    path.moveTo(points.first());
    if (points.size() == 1) {
        return path;
    }
    for (int index = 1; index < points.size() - 1; ++index) {
        const QPointF midpoint = (points[index] + points[index + 1]) * 0.5;
        path.quadTo(points[index], midpoint);
    }
    path.lineTo(points.last());
    return path;
}

QPainterPath smoothedPath(const QVector<StrokePoint> &points)
{
    QVector<QPointF> positions;
    positions.reserve(points.size());
    for (const StrokePoint &point : points) {
        positions.append(point.position);
    }
    return smoothedPath(positions);
}

qreal pressureScale(qreal dynamics, qreal pressure)
{
    const qreal normalizedDynamics = std::clamp(dynamics, 0.0, 1.0);
    return 1.0 - normalizedDynamics
        + std::clamp(pressure, 0.0, 1.0) * normalizedDynamics;
}

qreal pressureWidth(
    qreal baseWidth,
    qreal pressure,
    qreal sizeDynamics)
{
    return std::max(
        0.5,
        baseWidth * pressureScale(sizeDynamics, pressure));
}

QColor colorWithOpacity(const QColor &color, qreal opacity)
{
    QColor adjusted = color;
    adjusted.setAlpha(std::clamp(
        qRound(
            static_cast<qreal>(color.alpha())
            * std::clamp(opacity, 0.0, 1.0)),
        0,
        255));
    return adjusted;
}

void drawPath(
    QPainter &painter,
    const QPainterPath &path,
    const QColor &color,
    qreal width,
    BrushTipShape tipShape)
{
    const Qt::PenCapStyle capStyle =
        tipShape == BrushTipShape::Square
        ? Qt::SquareCap
        : Qt::RoundCap;
    const Qt::PenJoinStyle joinStyle =
        tipShape == BrushTipShape::Square
        ? Qt::MiterJoin
        : Qt::RoundJoin;
    painter.setPen(QPen(
        color,
        width,
        Qt::SolidLine,
        capStyle,
        joinStyle));
    painter.drawPath(path);
}

void drawLineDot(
    QPainter &painter,
    const StrokePoint &point,
    const QColor &color,
    qreal baseWidth,
    const BrushSettings &brush)
{
    const qreal width =
        pressureWidth(baseWidth, point.pressure, brush.sizeDynamics);
    const QColor adjusted = colorWithOpacity(
        color,
        brush.opacity
            * pressureScale(brush.opacityDynamics, point.pressure));
    painter.setPen(Qt::NoPen);
    painter.setBrush(adjusted);
    if (brush.tipShape == BrushTipShape::Square) {
        painter.drawRect(QRectF(
            point.position.x() - width * 0.5,
            point.position.y() - width * 0.5,
            width,
            width));
    } else {
        painter.drawEllipse(point.position, width * 0.5, width * 0.5);
    }
}

void drawLineStroke(
    QPainter &painter,
    const QVector<StrokePoint> &points,
    const QColor &color,
    qreal baseWidth,
    const BrushSettings &brush)
{
    if (points.size() < 2) {
        return;
    }

    qreal minimumPressure = 1.0;
    qreal maximumPressure = 0.0;
    for (const StrokePoint &point : points) {
        minimumPressure = std::min(minimumPressure, point.pressure);
        maximumPressure = std::max(maximumPressure, point.pressure);
    }

    if (maximumPressure - minimumPressure < 0.01) {
        const qreal pressure = points.first().pressure;
        drawPath(
            painter,
            smoothedPath(points),
            colorWithOpacity(
                color,
                brush.opacity
                    * pressureScale(brush.opacityDynamics, pressure)),
            pressureWidth(baseWidth, pressure, brush.sizeDynamics),
            brush.tipShape);
        return;
    }

    const QPointF firstMidpoint =
        (points[0].position + points[1].position) * 0.5;
    QPainterPath firstSegment;
    firstSegment.moveTo(points[0].position);
    firstSegment.lineTo(firstMidpoint);
    drawPath(
        painter,
        firstSegment,
        colorWithOpacity(
            color,
            brush.opacity
                * pressureScale(
                    brush.opacityDynamics,
                    (points[0].pressure + points[1].pressure) * 0.5)),
        pressureWidth(
            baseWidth,
            (points[0].pressure + points[1].pressure) * 0.5,
            brush.sizeDynamics),
        brush.tipShape);

    for (int index = 1; index < points.size() - 1; ++index) {
        const QPointF start =
            (points[index - 1].position + points[index].position) * 0.5;
        const QPointF end =
            (points[index].position + points[index + 1].position) * 0.5;
        QPainterPath segment;
        segment.moveTo(start);
        segment.quadTo(points[index].position, end);
        drawPath(
            painter,
            segment,
            colorWithOpacity(
                color,
                brush.opacity
                    * pressureScale(
                        brush.opacityDynamics,
                        points[index].pressure)),
            pressureWidth(
                baseWidth,
                points[index].pressure,
                brush.sizeDynamics),
            brush.tipShape);
    }

    const int lastIndex = static_cast<int>(points.size()) - 1;
    const QPointF lastMidpoint =
        (points[lastIndex - 1].position + points[lastIndex].position) * 0.5;
    QPainterPath lastSegment;
    lastSegment.moveTo(lastMidpoint);
    lastSegment.lineTo(points[lastIndex].position);
    drawPath(
        painter,
        lastSegment,
        colorWithOpacity(
            color,
            brush.opacity
                * pressureScale(
                    brush.opacityDynamics,
                    (points[lastIndex - 1].pressure
                     + points[lastIndex].pressure)
                        * 0.5)),
        pressureWidth(
            baseWidth,
            (points[lastIndex - 1].pressure
             + points[lastIndex].pressure)
                * 0.5,
            brush.sizeDynamics),
        brush.tipShape);
}

void drawAirbrushDab(
    QPainter &painter,
    const QPointF &position,
    qreal diameter,
    const QColor &color,
    qreal hardness,
    BrushTipShape tipShape)
{
    if (diameter <= 0.0 || color.alpha() <= 0) {
        return;
    }
    const QRectF bounds(
        position.x() - diameter * 0.5,
        position.y() - diameter * 0.5,
        diameter,
        diameter);
    painter.setPen(Qt::NoPen);
    if (tipShape == BrushTipShape::Square || hardness >= 0.995) {
        painter.setBrush(color);
        if (tipShape == BrushTipShape::Square) {
            painter.drawRect(bounds);
        } else {
            painter.drawEllipse(bounds);
        }
        return;
    }

    QRadialGradient gradient(position, diameter * 0.5);
    const qreal innerStop = std::clamp(hardness, 0.0, 0.98);
    gradient.setColorAt(0.0, color);
    if (innerStop > 0.001) {
        gradient.setColorAt(innerStop, color);
    }
    QColor edge = color;
    edge.setAlpha(0);
    gradient.setColorAt(1.0, edge);
    painter.setBrush(gradient);
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.drawEllipse(bounds);
    painter.restore();
}

void drawAirbrushStroke(
    QPainter &painter,
    const QVector<StrokePoint> &points,
    const QColor &color,
    qreal baseWidth,
    const BrushSettings &brush)
{
    for (const StrokePoint &point : points) {
        const qreal diameter =
            pressureWidth(baseWidth, point.pressure, brush.sizeDynamics);
        const QColor dabColor = colorWithOpacity(
            color,
            brush.opacity
                * brush.flow
                * pressureScale(brush.opacityDynamics, point.pressure));
        drawAirbrushDab(
            painter,
            point.position,
            diameter,
            dabColor,
            brush.hardness,
            brush.tipShape);
    }
}

qreal unitNoise(quint64 seed, int frame, int index, quint64 channel)
{
    return (signedNoise(seed, frame, index, channel) + 1.0) * 0.5;
}

void drawSprayStroke(
    QPainter &painter,
    const QVector<StrokePoint> &points,
    const QColor &color,
    qreal baseWidth,
    const BrushSettings &brush,
    quint64 seed,
    int frameIndex)
{
    const int particlesPerPoint = std::clamp(
        qRound(brush.density * 6.0),
        1,
        24);
    const int noiseFrame = brush.animatedJitter ? frameIndex : 0;
    qsizetype emittedParticles = 0;
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(Qt::NoPen);

    for (int pointIndex = 0; pointIndex < points.size(); ++pointIndex) {
        const StrokePoint &point = points[pointIndex];
        const qreal pressureSize =
            pressureScale(brush.sizeDynamics, point.pressure);
        const QColor particleColor = colorWithOpacity(
            color,
            brush.opacity
                * brush.flow
                * pressureScale(brush.opacityDynamics, point.pressure));
        painter.setBrush(particleColor);

        for (int particleIndex = 0;
             particleIndex < particlesPerPoint;
             ++particleIndex) {
            if (emittedParticles >= maximumSprayParticles) {
                return;
            }
            ++emittedParticles;
            const int noiseIndex =
                pointIndex * particlesPerPoint + particleIndex;
            const qreal angle =
                unitNoise(seed, noiseFrame, noiseIndex, 0x36d1a53bULL)
                * std::numbers::pi * 2.0;
            const qreal radius =
                std::sqrt(unitNoise(
                    seed,
                    noiseFrame,
                    noiseIndex,
                    0x9c8e31d7ULL))
                * brush.scatter * baseWidth * 0.5;
            const QPointF position =
                point.position
                + QPointF(std::cos(angle), std::sin(angle)) * radius;
            const qreal jitter =
                1.0
                + signedNoise(
                    seed,
                    noiseFrame,
                    noiseIndex,
                    0xa24baed4ULL)
                    * brush.sizeJitter * 0.75;
            const qreal particleDiameter = std::max(
                0.5,
                baseWidth
                    * brush.particleSize
                    * pressureSize
                    * std::max(0.1, jitter));
            const QRectF bounds(
                position.x() - particleDiameter * 0.5,
                position.y() - particleDiameter * 0.5,
                particleDiameter,
                particleDiameter);
            if (brush.tipShape == BrushTipShape::Square) {
                painter.drawRect(bounds);
            } else {
                painter.drawEllipse(bounds);
            }
        }
    }
}

QPainterPath maskPath(const QImage &mask)
{
    QPainterPath path;
    if (mask.isNull()
        || mask.format() != QImage::Format_Grayscale8) {
        return path;
    }
    for (int y = 0; y < mask.height(); ++y) {
        const uchar *line = mask.constScanLine(y);
        int x = 0;
        while (x < mask.width()) {
            while (x < mask.width() && line[x] < 128) {
                ++x;
            }
            const int left = x;
            while (x < mask.width() && line[x] >= 128) {
                ++x;
            }
            if (left < x) {
                path.addRect(left, y, x - left, 1);
            }
        }
    }
    return path;
}

}

QImage RenderEngine::fillRegionMask(const QImage &image, const QPoint &seed)
{
    if (image.isNull()
        || image.format() != QImage::Format_ARGB32_Premultiplied
        || !image.rect().contains(seed)) {
        return {};
    }
    const int width = image.width();
    const int height = image.height();
    auto blocked = [&image](int x, int y) {
        const QRgb *line =
            reinterpret_cast<const QRgb *>(image.constScanLine(y));
        return qAlpha(line[x]) >= 128;
    };
    if (blocked(seed.x(), seed.y())) {
        return {};
    }

    QImage mask(width, height, QImage::Format_Grayscale8);
    mask.fill(0);
    QVector<QPoint> pending;
    pending.append(seed);
    while (!pending.isEmpty()) {
        const QPoint point = pending.takeLast();
        const int y = point.y();
        uchar *maskLine = mask.scanLine(y);
        if (maskLine[point.x()] || blocked(point.x(), y)) {
            continue;
        }
        int left = point.x();
        while (left > 0 && !maskLine[left - 1] && !blocked(left - 1, y)) {
            --left;
        }
        int right = point.x();
        while (right < width - 1
               && !maskLine[right + 1]
               && !blocked(right + 1, y)) {
            ++right;
        }
        for (int x = left; x <= right; ++x) {
            maskLine[x] = 255;
        }
        for (const int neighborY : {y - 1, y + 1}) {
            if (neighborY < 0 || neighborY >= height) {
                continue;
            }
            const uchar *neighborMask = mask.constScanLine(neighborY);
            for (int x = left; x <= right; ++x) {
                if (!neighborMask[x] && !blocked(x, neighborY)) {
                    pending.append(QPoint(x, neighborY));
                    while (x < right
                           && !neighborMask[x + 1]
                           && !blocked(x + 1, neighborY)) {
                        ++x;
                    }
                }
            }
        }
    }
    return mask;
}

namespace {

void applyFillStroke(
    QImage &layerImage,
    const Stroke &stroke,
    const QImage &clipMask,
    qreal horizontalScale,
    qreal verticalScale)
{
    const QPointF seedPosition = stroke.points.first().position;
    const QPoint seed(
        std::clamp(
            static_cast<int>(seedPosition.x() * horizontalScale),
            0,
            layerImage.width() - 1),
        std::clamp(
            static_cast<int>(seedPosition.y() * verticalScale),
            0,
            layerImage.height() - 1));
    const QImage mask = RenderEngine::fillRegionMask(layerImage, seed);
    if (mask.isNull()) {
        return;
    }
    const QRgb fill = qPremultiply(stroke.color.rgba());
    const int fillRed = qRed(fill);
    const int fillGreen = qGreen(fill);
    const int fillBlue = qBlue(fill);
    const int fillAlpha = qAlpha(fill);
    const int width = layerImage.width();
    const int height = layerImage.height();

    for (int y = 0; y < height; ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(layerImage.scanLine(y));
        const uchar *maskLine = mask.constScanLine(y);
        const uchar *maskAbove = y > 0 ? mask.constScanLine(y - 1) : nullptr;
        const uchar *maskBelow =
            y < height - 1 ? mask.constScanLine(y + 1) : nullptr;
        const uchar *clipLine = clipMask.isNull()
            ? nullptr
            : clipMask.constScanLine(y);
        for (int x = 0; x < width; ++x) {
            if (clipLine && clipLine[x] < 128) {
                continue;
            }
            if (maskLine[x]) {
                line[x] = fill;
                continue;
            }
            const bool touchesRegion =
                (x > 0 && maskLine[x - 1])
                || (x < width - 1 && maskLine[x + 1])
                || (maskAbove && maskAbove[x])
                || (maskBelow && maskBelow[x]);
            if (!touchesRegion) {
                continue;
            }
            const QRgb existing = line[x];
            const int inverse = 255 - qAlpha(existing);
            line[x] = qRgba(
                qRed(existing) + fillRed * inverse / 255,
                qGreen(existing) + fillGreen * inverse / 255,
                qBlue(existing) + fillBlue * inverse / 255,
                qAlpha(existing) + fillAlpha * inverse / 255);
        }
    }
}

void renderLayerStrokes(
    QImage &layerImage,
    const Document &document,
    const QVector<Stroke> &strokes,
    int normalizedFrame,
    int frameCount,
    qreal horizontalScale,
    qreal verticalScale,
    QHash<qint64, QPainterPath> &clipPaths,
    QHash<qint64, QImage> &scaledClipMasks)
{
    const QSize outputSize = layerImage.size();
    QPainter painter(&layerImage);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.scale(horizontalScale, verticalScale);

    for (const Stroke &stroke : strokes) {
        if (stroke.points.isEmpty()
            || !isValidBrushSettings(stroke.brush)) {
            continue;
        }

        if (stroke.mode == StrokeMode::Fill) {
            QImage scaledClipMask;
            if (!stroke.clipMask.isNull()) {
                const qint64 key = stroke.clipMask.cacheKey();
                const auto cached = scaledClipMasks.constFind(key);
                if (cached != scaledClipMasks.cend()) {
                    scaledClipMask = cached.value();
                } else {
                    scaledClipMask = stroke.clipMask.size() == outputSize
                        ? stroke.clipMask
                        : stroke.clipMask.scaled(
                            outputSize,
                            Qt::IgnoreAspectRatio,
                            Qt::FastTransformation);
                    if (scaledClipMask.isNull()) {
                        continue;
                    }
                    scaledClipMasks.insert(key, scaledClipMask);
                }
            }
            painter.end();
            applyFillStroke(
                layerImage,
                stroke,
                scaledClipMask,
                horizontalScale,
                verticalScale);
            painter.begin(&layerImage);
            painter.setRenderHint(QPainter::Antialiasing, false);
            painter.scale(horizontalScale, verticalScale);
            continue;
        }

        painter.save();
        painter.setRenderHint(
            QPainter::Antialiasing,
            stroke.brush.antialiasing);
        if (!stroke.clipMask.isNull()) {
            const qint64 key = stroke.clipMask.cacheKey();
            auto cached = clipPaths.constFind(key);
            if (cached == clipPaths.cend()) {
                cached = clipPaths.insert(
                    key,
                    maskPath(stroke.clipMask));
            }
            painter.setClipPath(
                cached.value(),
                Qt::IntersectClip);
        }
        painter.setCompositionMode(
            stroke.mode == StrokeMode::Erase
                ? QPainter::CompositionMode_DestinationOut
                : QPainter::CompositionMode_SourceOver);

        const qreal strokeWobble =
            document.wobbleAmount * stroke.brush.wobbleScale;
        const qreal widthNoiseScale = std::clamp(
            strokeWobble / 1.6,
            0.0,
            1.0);
        const qreal frameWidthNoise =
            widthNoiseScale
            * signedNoise(
                stroke.seed,
                normalizedFrame,
                0,
                0x1a67d3c4ULL);
        const qreal width = std::max(
            0.5,
            stroke.width * (1.0 + frameWidthNoise * 0.025));
        painter.setBrush(Qt::NoBrush);
        const QColor strokeColor =
            stroke.mode == StrokeMode::Erase ? Qt::black : stroke.color;
        const qreal brushSpacing =
            stroke.brush.engine == BrushEngine::Line
            ? -1.0
            : std::max(0.5, width * stroke.brush.spacing);
        const qsizetype maximumPoints =
            stroke.brush.engine == BrushEngine::Line
            ? maximumResampledPoints
            : maximumBrushDabs;
        const QVector<StrokePoint> points = displacedStrokePoints(
            stroke,
            normalizedFrame,
            frameCount,
            strokeWobble,
            brushSpacing,
            maximumPoints);
        if (points.isEmpty()) {
            painter.restore();
            continue;
        }

        switch (stroke.brush.engine) {
        case BrushEngine::Line:
            if (points.size() == 1) {
                drawLineDot(
                    painter,
                    points.first(),
                    strokeColor,
                    width,
                    stroke.brush);
            } else {
                drawLineStroke(
                    painter,
                    points,
                    strokeColor,
                    width,
                    stroke.brush);
            }
            break;
        case BrushEngine::Airbrush:
            drawAirbrushStroke(
                painter,
                points,
                strokeColor,
                width,
                stroke.brush);
            break;
        case BrushEngine::Spray:
            drawSprayStroke(
                painter,
                points,
                strokeColor,
                width,
                stroke.brush,
                stroke.seed,
                normalizedFrame);
            break;
        }
        painter.restore();
    }

    painter.end();
}

QImage renderAtSize(
    const Document &document,
    int frameIndex,
    const QSize &outputSize)
{
    if (!document.size.isValid() || !outputSize.isValid()) {
        return {};
    }

    QImage result(outputSize, QImage::Format_ARGB32_Premultiplied);
    if (result.isNull()) {
        return {};
    }
    result.fill(document.background);
    const qreal horizontalScale =
        static_cast<qreal>(outputSize.width()) / document.size.width();
    const qreal verticalScale =
        static_cast<qreal>(outputSize.height()) / document.size.height();

    const int frameCount = std::max(1, document.animationFrames);
    const int normalizedFrame =
        ((frameIndex % frameCount) + frameCount) % frameCount;

    QPainter compositor(&result);
    compositor.setRenderHint(QPainter::Antialiasing, false);
    QHash<qint64, QPainterPath> clipPaths;
    QHash<qint64, QImage> scaledClipMasks;

    for (const Layer &layer : document.layers) {
        if (!layer.visible
            || layer.opacity <= 0.0
            || layer.strokes.isEmpty()) {
            continue;
        }

        QImage layerImage(outputSize, QImage::Format_ARGB32_Premultiplied);
        if (layerImage.isNull()) {
            return {};
        }
        layerImage.fill(Qt::transparent);
        renderLayerStrokes(
            layerImage,
            document,
            layer.strokes,
            normalizedFrame,
            frameCount,
            horizontalScale,
            verticalScale,
            clipPaths,
            scaledClipMasks);
        compositor.setOpacity(std::clamp(layer.opacity, 0.0, 1.0));
        compositor.drawImage(QPoint(0, 0), layerImage);
    }

    return result;
}

}

QImage RenderEngine::render(const Document &document, int frameIndex)
{
    return renderAtSize(document, frameIndex, document.size);
}

QImage RenderEngine::renderScaled(
    const Document &document,
    int frameIndex,
    const QSize &outputSize)
{
    return renderAtSize(document, frameIndex, outputSize);
}

RenderEngine::LayerSplitFrame RenderEngine::renderLayerSplit(
    const Document &document,
    int frameIndex,
    const QSize &outputSize,
    const QUuid &layerId)
{
    LayerSplitFrame split;
    if (!document.size.isValid()
        || !outputSize.isValid()
        || document.layerIndex(layerId) < 0) {
        return split;
    }

    QImage below(outputSize, QImage::Format_ARGB32_Premultiplied);
    QImage layerBase(outputSize, QImage::Format_ARGB32_Premultiplied);
    if (below.isNull() || layerBase.isNull()) {
        return split;
    }
    below.fill(document.background);
    layerBase.fill(Qt::transparent);
    QImage above;

    const qreal horizontalScale =
        static_cast<qreal>(outputSize.width()) / document.size.width();
    const qreal verticalScale =
        static_cast<qreal>(outputSize.height()) / document.size.height();
    const int frameCount = std::max(1, document.animationFrames);
    const int normalizedFrame =
        ((frameIndex % frameCount) + frameCount) % frameCount;
    QHash<qint64, QPainterPath> clipPaths;
    QHash<qint64, QImage> scaledClipMasks;

    bool afterTarget = false;
    for (const Layer &layer : document.layers) {
        if (layer.id == layerId) {
            split.layerVisible = layer.visible && layer.opacity > 0.0;
            split.layerOpacity = std::clamp(layer.opacity, 0.0, 1.0);
            if (split.layerVisible && !layer.strokes.isEmpty()) {
                renderLayerStrokes(
                    layerBase,
                    document,
                    layer.strokes,
                    normalizedFrame,
                    frameCount,
                    horizontalScale,
                    verticalScale,
                    clipPaths,
                    scaledClipMasks);
            }
            afterTarget = true;
            continue;
        }
        if (!layer.visible
            || layer.opacity <= 0.0
            || layer.strokes.isEmpty()) {
            continue;
        }

        QImage layerImage(outputSize, QImage::Format_ARGB32_Premultiplied);
        if (layerImage.isNull()) {
            return split;
        }
        layerImage.fill(Qt::transparent);
        renderLayerStrokes(
            layerImage,
            document,
            layer.strokes,
            normalizedFrame,
            frameCount,
            horizontalScale,
            verticalScale,
            clipPaths,
            scaledClipMasks);
        if (afterTarget && above.isNull()) {
            above = QImage(outputSize, QImage::Format_ARGB32_Premultiplied);
            if (above.isNull()) {
                return split;
            }
            above.fill(Qt::transparent);
        }
        QPainter compositor(afterTarget ? &above : &below);
        compositor.setRenderHint(QPainter::Antialiasing, false);
        compositor.setOpacity(std::clamp(layer.opacity, 0.0, 1.0));
        compositor.drawImage(QPoint(0, 0), layerImage);
    }

    split.below = std::move(below);
    split.layerBase = std::move(layerBase);
    split.above = std::move(above);
    split.valid = true;
    return split;
}

bool RenderEngine::renderStrokesOnLayer(
    QImage &layerImage,
    const Document &document,
    const QVector<Stroke> &strokes,
    int frameIndex,
    const QSize &outputSize)
{
    if (!document.size.isValid() || !outputSize.isValid()) {
        return false;
    }
    if (layerImage.isNull()) {
        layerImage = QImage(outputSize, QImage::Format_ARGB32_Premultiplied);
        if (layerImage.isNull()) {
            return false;
        }
        layerImage.fill(Qt::transparent);
    }
    if (layerImage.size() != outputSize
        || layerImage.format() != QImage::Format_ARGB32_Premultiplied) {
        return false;
    }

    const qreal horizontalScale =
        static_cast<qreal>(outputSize.width()) / document.size.width();
    const qreal verticalScale =
        static_cast<qreal>(outputSize.height()) / document.size.height();
    const int frameCount = std::max(1, document.animationFrames);
    const int normalizedFrame =
        ((frameIndex % frameCount) + frameCount) % frameCount;
    QHash<qint64, QPainterPath> clipPaths;
    QHash<qint64, QImage> scaledClipMasks;
    renderLayerStrokes(
        layerImage,
        document,
        strokes,
        normalizedFrame,
        frameCount,
        horizontalScale,
        verticalScale,
        clipPaths,
        scaledClipMasks);
    return true;
}

QImage RenderEngine::composeLayerSplit(
    const LayerSplitFrame &split,
    const QImage &layerImage)
{
    if (!split.valid || split.below.isNull()) {
        return {};
    }
    QImage result = split.below;
    QPainter compositor(&result);
    compositor.setRenderHint(QPainter::Antialiasing, false);
    if (split.layerVisible && !layerImage.isNull()) {
        compositor.setOpacity(split.layerOpacity);
        compositor.drawImage(QPoint(0, 0), layerImage);
        compositor.setOpacity(1.0);
    }
    if (!split.above.isNull()) {
        compositor.drawImage(QPoint(0, 0), split.above);
    }
    return result;
}

QPainterPath RenderEngine::strokePath(
    const Stroke &stroke,
    int frameIndex,
    int frameCount,
    qreal wobbleAmount)
{
    return smoothedPath(displacedStrokePoints(
        stroke,
        frameIndex,
        frameCount,
        wobbleAmount * stroke.brush.wobbleScale));
}

}
