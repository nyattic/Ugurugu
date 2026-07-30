#include "render/RenderEngine.hpp"

#include "document/DocumentLimits.hpp"

#include <QPainter>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace wobble {

namespace {

constexpr qsizetype maximumResampledPoints =
    DocumentLimits::maximumPointsPerStroke;

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

QVector<StrokePoint> resample(const QVector<StrokePoint> &source, qreal spacing)
{
    if (!std::isfinite(spacing)
        || spacing <= 0.0
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
        totalLength / static_cast<long double>(maximumResampledPoints - 1);
    spacing = std::max(
        spacing,
        static_cast<qreal>(minimumSpacing));

    QVector<StrokePoint> result;
    result.reserve(std::min(
        maximumResampledPoints,
        source.size() * 2));
    result.append(source.first());

    StrokePoint previous = source.first();
    qreal distanceToNext = spacing;

    for (int index = 1; index < source.size(); ++index) {
        const StrokePoint target = source[index];
        QPointF delta = target.position - previous.position;
        qreal segmentLength = std::hypot(delta.x(), delta.y());

        while (segmentLength >= distanceToNext && segmentLength > 0.0) {
            if (result.size() >= maximumResampledPoints - 1) {
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
        && result.size() < maximumResampledPoints) {
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
    qreal wobbleAmount)
{
    if (stroke.points.isEmpty()) {
        return {};
    }

    const int normalizedCount = std::max(1, frameCount);
    const int normalizedFrame =
        ((frameIndex % normalizedCount) + normalizedCount) % normalizedCount;
    const qreal spacing = std::clamp(stroke.width * 0.55, 2.0, 5.0);
    QVector<StrokePoint> samples = resample(stroke.points, spacing);
    const qreal amplitude =
        std::max(0.0, wobbleAmount)
        * (0.82 + std::min(stroke.width, 40.0) * 0.018);
    constexpr qreal swayWavelength = 26.0;
    constexpr qreal detailWavelength = 9.0;

    qreal arcLength = 0.0;
    for (int index = 0; index < samples.size(); ++index) {
        const QPointF before = samples[std::max(0, index - 1)].position;
        const QPointF after = samples[
            std::min(static_cast<int>(samples.size()) - 1, index + 1)].position;
        if (index > 0) {
            const QPointF step =
                samples[index].position - samples[index - 1].position;
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

qreal pressureWidth(qreal baseWidth, qreal pressure)
{
    return std::max(
        0.5,
        baseWidth * (0.2 + std::clamp(pressure, 0.0, 1.0) * 0.8));
}

void drawPath(
    QPainter &painter,
    const QPainterPath &path,
    const QColor &color,
    qreal width)
{
    painter.setPen(QPen(
        color,
        width,
        Qt::SolidLine,
        Qt::RoundCap,
        Qt::RoundJoin));
    painter.drawPath(path);
}

void drawPressureStroke(
    QPainter &painter,
    const QVector<StrokePoint> &points,
    const QColor &color,
    qreal baseWidth)
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
        drawPath(
            painter,
            smoothedPath(points),
            color,
            pressureWidth(baseWidth, points.first().pressure));
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
        color,
        pressureWidth(
            baseWidth,
            (points[0].pressure + points[1].pressure) * 0.5));

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
            color,
            pressureWidth(baseWidth, points[index].pressure));
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
        color,
        pressureWidth(
            baseWidth,
            (points[lastIndex - 1].pressure
             + points[lastIndex].pressure)
                * 0.5));
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

void applyFillStroke(QImage &layerImage, const Stroke &stroke)
{
    const QPointF seedPosition = stroke.points.first().position;
    const QPoint seed(
        std::clamp(
            static_cast<int>(seedPosition.x()),
            0,
            layerImage.width() - 1),
        std::clamp(
            static_cast<int>(seedPosition.y()),
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
        for (int x = 0; x < width; ++x) {
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

}

QImage RenderEngine::render(const Document &document, int frameIndex)
{
    if (!document.size.isValid()) {
        return {};
    }

    QImage result(document.size, QImage::Format_ARGB32_Premultiplied);
    if (result.isNull()) {
        return {};
    }
    result.fill(document.background);

    const int frameCount = std::max(1, document.animationFrames);
    const int normalizedFrame = ((frameIndex % frameCount) + frameCount) % frameCount;

    QPainter compositor(&result);
    compositor.setRenderHint(QPainter::Antialiasing, true);

    for (const Layer &layer : document.layers) {
        if (!layer.visible
            || layer.opacity <= 0.0
            || layer.strokes.isEmpty()) {
            continue;
        }

        QImage layerImage(document.size, QImage::Format_ARGB32_Premultiplied);
        if (layerImage.isNull()) {
            return {};
        }
        layerImage.fill(Qt::transparent);
        QPainter painter(&layerImage);
        painter.setRenderHint(QPainter::Antialiasing, true);

        for (const Stroke &stroke : layer.strokes) {
            if (stroke.points.isEmpty()) {
                continue;
            }

            if (stroke.mode == StrokeMode::Fill) {
                painter.end();
                applyFillStroke(layerImage, stroke);
                painter.begin(&layerImage);
                painter.setRenderHint(QPainter::Antialiasing, true);
                continue;
            }

            painter.setCompositionMode(
                stroke.mode == StrokeMode::Erase
                    ? QPainter::CompositionMode_DestinationOut
                    : QPainter::CompositionMode_SourceOver);

            const qreal widthNoiseScale = std::clamp(
                document.wobbleAmount / 1.6,
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
            const QVector<StrokePoint> points = displacedStrokePoints(
                stroke,
                normalizedFrame,
                frameCount,
                document.wobbleAmount);
            if (points.isEmpty()) {
                continue;
            }

            if (points.size() == 1) {
                const qreal dotWidth =
                    pressureWidth(width, points.first().pressure);
                painter.setPen(Qt::NoPen);
                painter.setBrush(strokeColor);
                painter.drawEllipse(
                    points.first().position,
                    dotWidth * 0.5,
                    dotWidth * 0.5);
            } else {
                drawPressureStroke(
                    painter,
                    points,
                    strokeColor,
                    width);
            }
        }

        painter.end();
        compositor.setOpacity(std::clamp(layer.opacity, 0.0, 1.0));
        compositor.drawImage(QPoint(0, 0), layerImage);
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
        wobbleAmount));
}

}
