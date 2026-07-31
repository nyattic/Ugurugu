#include "brush/BrushPreset.hpp"
#include "document/DocumentController.hpp"
#include "document/DocumentLimits.hpp"
#include "document/SelectionOperation.hpp"
#include "render/RenderEngine.hpp"

#include <QPainter>
#include <QSet>
#include <QTransform>
#include <QtTest>

#include <algorithm>
#include <cmath>
#include <limits>

namespace wobble {

namespace {

Stroke makeStroke(
    StrokeMode mode,
    const QColor &color,
    qreal width,
    quint64 seed,
    const QVector<QPointF> &positions)
{
    Stroke stroke;
    stroke.mode = mode;
    stroke.color = color;
    stroke.width = width;
    stroke.seed = seed;
    for (const QPointF &position : positions) {
        stroke.points.append({position, 1.0});
    }
    return stroke;
}

Document animatedDocument()
{
    Document document = Document::createDefault(QSize(96, 72));
    document.animationFrames = 12;
    document.wobbleAmount = 6.0;
    document.layers.first().strokes.append(makeStroke(
        StrokeMode::Paint,
        QColor(25, 40, 70),
        9.0,
        0x123456789abcdef0ULL,
        {
            QPointF(8.0, 52.0),
            QPointF(24.0, 18.0),
            QPointF(48.0, 46.0),
            QPointF(72.0, 14.0),
            QPointF(88.0, 48.0)
        }));
    return document;
}

QImage rectangularMask(const QSize &size, const QRect &rect)
{
    QImage mask(size, QImage::Format_Grayscale8);
    mask.fill(0);
    const QRect clipped = rect.intersected(mask.rect());
    for (int y = clipped.top(); y <= clipped.bottom(); ++y) {
        std::fill(
            mask.scanLine(y) + clipped.left(),
            mask.scanLine(y) + clipped.right() + 1,
            255);
    }
    return mask;
}

QImage activeLayerPixels(
    const Document &document,
    int frameIndex = 0)
{
    QImage layerImage;
    if (document.layers.isEmpty()
        || !RenderEngine::renderStrokesOnLayer(
            layerImage,
            document,
            document.layers.first().strokes,
            frameIndex,
            document.size)) {
        return {};
    }
    return layerImage;
}

QImage rasterSelectionResult(
    const QImage &before,
    const QImage &selection,
    const QPoint &delta,
    bool cutSource)
{
    if (before.isNull()
        || selection.size() != before.size()
        || selection.format() != QImage::Format_Grayscale8) {
        return {};
    }

    QImage payload(before.size(), QImage::Format_ARGB32_Premultiplied);
    payload.fill(Qt::transparent);
    QImage result = before;
    for (int y = 0; y < before.height(); ++y) {
        const QRgb *source =
            reinterpret_cast<const QRgb *>(before.constScanLine(y));
        QRgb *payloadLine =
            reinterpret_cast<QRgb *>(payload.scanLine(y));
        QRgb *resultLine =
            reinterpret_cast<QRgb *>(result.scanLine(y));
        const uchar *selectionLine = selection.constScanLine(y);
        for (int x = 0; x < before.width(); ++x) {
            if (selectionLine[x] < 128) {
                continue;
            }
            payloadLine[x] = source[x];
            if (cutSource) {
                resultLine[x] = 0;
            }
        }
    }

    QPainter painter(&result);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    painter.drawImage(delta, payload);
    painter.end();
    return result;
}

QImage rasterSelectionTransformResult(
    const QImage &before,
    const QImage &selection,
    const QTransform &transform,
    bool cutSource,
    bool smooth = false)
{
    if (before.isNull()
        || selection.size() != before.size()
        || selection.format() != QImage::Format_Grayscale8
        || !transform.isInvertible()) {
        return {};
    }

    QImage payload(before.size(), QImage::Format_ARGB32_Premultiplied);
    payload.fill(Qt::transparent);
    QImage result = before;
    for (int y = 0; y < before.height(); ++y) {
        const QRgb *source =
            reinterpret_cast<const QRgb *>(before.constScanLine(y));
        QRgb *payloadLine =
            reinterpret_cast<QRgb *>(payload.scanLine(y));
        QRgb *resultLine =
            reinterpret_cast<QRgb *>(result.scanLine(y));
        const uchar *selectionLine = selection.constScanLine(y);
        for (int x = 0; x < before.width(); ++x) {
            if (selectionLine[x] < 128) {
                continue;
            }
            payloadLine[x] = source[x];
            if (cutSource) {
                resultLine[x] = 0;
            }
        }
    }

    QPainter painter(&result);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, smooth);
    painter.setTransform(transform);
    painter.drawImage(QPointF(), payload);
    painter.end();
    return result;
}

QImage transformedSelectionMask(
    const QImage &selection,
    const QTransform &transform)
{
    if (selection.isNull()
        || selection.format() != QImage::Format_Grayscale8
        || !transform.isInvertible()) {
        return {};
    }
    QImage transformed(selection.size(), QImage::Format_Grayscale8);
    if (transformed.isNull()) {
        return {};
    }
    transformed.fill(0);
    QPainter painter(&transformed);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter.setTransform(transform);
    painter.drawImage(QPointF(), selection);
    painter.end();
    return transformed;
}

QImage renderAdditionalStrokes(
    QImage base,
    const Document &document,
    const QVector<Stroke> &strokes,
    int frameIndex)
{
    if (!RenderEngine::renderStrokesOnLayer(
            base,
            document,
            strokes,
            frameIndex,
            base.size())) {
        return {};
    }
    return base;
}

QImage resizedRasterResult(
    const QImage &source,
    const QSize &targetSize,
    bool smooth)
{
    if (source.isNull() || !targetSize.isValid()) {
        return {};
    }
    QImage target(targetSize, QImage::Format_ARGB32_Premultiplied);
    if (target.isNull()) {
        return {};
    }
    target.fill(Qt::transparent);
    QPainter painter(&target);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, smooth);
    painter.drawImage(
        QRectF(QPointF(), QSizeF(targetSize)),
        source,
        QRectF(QPointF(), QSizeF(source.size())));
    painter.end();
    return target;
}

QImage reframedRasterResult(
    const QImage &source,
    const QSize &targetSize,
    const QPoint &contentOffset)
{
    if (source.isNull() || !targetSize.isValid()) {
        return {};
    }
    QImage target(targetSize, QImage::Format_ARGB32_Premultiplied);
    if (target.isNull()) {
        return {};
    }
    target.fill(Qt::transparent);
    QPainter painter(&target);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.drawImage(contentOffset, source);
    painter.end();
    return target;
}

QImage clearedSelectionResult(
    const QImage &before,
    const QImage &selection)
{
    if (before.isNull()
        || selection.size() != before.size()
        || selection.format() != QImage::Format_Grayscale8) {
        return {};
    }
    QImage result = before;
    for (int y = 0; y < result.height(); ++y) {
        QRgb *resultLine =
            reinterpret_cast<QRgb *>(result.scanLine(y));
        const uchar *selectionLine = selection.constScanLine(y);
        for (int x = 0; x < result.width(); ++x) {
            if (selectionLine[x] >= 128) {
                resultLine[x] = 0;
            }
        }
    }
    return result;
}

struct ImageDifference {
    quint64 channelDifference = 0;
    quint64 comparedChannels = 0;
    qsizetype visiblyDifferentPixels = 0;
};

ImageDifference imageDifference(
    const QImage &left,
    const QImage &right,
    int visibleThreshold = 24)
{
    ImageDifference difference;
    if (left.isNull()
        || right.isNull()
        || left.size() != right.size()) {
        difference.channelDifference =
            std::numeric_limits<quint64>::max();
        return difference;
    }
    difference.comparedChannels =
        static_cast<quint64>(left.width())
        * static_cast<quint64>(left.height()) * 4;
    for (int y = 0; y < left.height(); ++y) {
        for (int x = 0; x < left.width(); ++x) {
            const QColor a = left.pixelColor(x, y);
            const QColor b = right.pixelColor(x, y);
            const int red = std::abs(a.red() - b.red());
            const int green = std::abs(a.green() - b.green());
            const int blue = std::abs(a.blue() - b.blue());
            const int alpha = std::abs(a.alpha() - b.alpha());
            difference.channelDifference +=
                static_cast<quint64>(red + green + blue + alpha);
            if (std::max({red, green, blue, alpha})
                > visibleThreshold) {
                ++difference.visiblyDifferentPixels;
            }
        }
    }
    return difference;
}

}

class RenderEngineTests final : public QObject
{
    Q_OBJECT

private slots:
    void rendersDeterministically()
    {
        const Document document = animatedDocument();
        const QImage first = RenderEngine::render(document, 5);
        const QImage second = RenderEngine::render(document, 5);

        QVERIFY(!first.isNull());
        QCOMPARE(first.size(), document.size);
        QVERIFY(first == second);
    }

    void rendersTheBackgroundWithoutLayers()
    {
        Document document = Document::createDefault(QSize(32, 24));
        document.background = QColor(12, 34, 56);
        document.layers.clear();
        document.activeLayerId = {};

        const QImage rendered = RenderEngine::render(document, 0);
        QVERIFY(!rendered.isNull());
        QCOMPARE(rendered.size(), document.size);
        QCOMPARE(rendered.pixelColor(0, 0), document.background);
        QCOMPARE(
            rendered.pixelColor(
                rendered.width() - 1,
                rendered.height() - 1),
            document.background);
    }

    void rendersScaledPreview()
    {
        const Document document = animatedDocument();
        const QSize previewSize(48, 36);
        RenderEngine::ScaledRenderStats stats;
        const QImage preview =
            RenderEngine::renderScaled(
                document,
                5,
                previewSize,
                RenderEngine::ScaledRenderMode::DisplayPreview,
                &stats);

        QVERIFY(!preview.isNull());
        QCOMPARE(preview.size(), previewSize);
        QVERIFY(stats.usedDisplayScaleReplay);
        QVERIFY(!stats.usedNativeExactFallback);
        bool containsPaint = false;
        for (int y = 0; y < preview.height() && !containsPaint; ++y) {
            for (int x = 0; x < preview.width(); ++x) {
                if (preview.pixelColor(x, y) != document.background) {
                    containsPaint = true;
                    break;
                }
            }
        }
        QVERIFY(containsPaint);
        QVERIFY(RenderEngine::renderScaled(document, 0, {}).isNull());

        const QImage exact = RenderEngine::renderScaled(
            document,
            5,
            previewSize,
            RenderEngine::ScaledRenderMode::NativeExact,
            &stats);
        QVERIFY(!exact.isNull());
        QVERIFY(!stats.usedDisplayScaleReplay);
        QVERIFY(stats.usedNativeExactFallback);

        const QImage nativeSized = RenderEngine::renderScaled(
            document,
            5,
            document.size,
            RenderEngine::ScaledRenderMode::DisplayPreview,
            &stats);
        QCOMPARE(nativeSized, RenderEngine::render(document, 5));
        QVERIFY(!stats.usedDisplayScaleReplay);
        QVERIFY(stats.usedNativeExactFallback);
    }

    void replaysIntegralNearestSelectionAtDisplayScale()
    {
        Document document = Document::createDefault(QSize(128, 96));
        document.background = Qt::transparent;
        document.wobbleAmount = 0.0;
        Layer &layer = document.layers.first();
        layer.initialCanvasSize = document.size;

        Stroke block = makeStroke(
            StrokeMode::Paint,
            QColor(210, 40, 70),
            32.0,
            901,
            {QPointF(24.0, 24.0)});
        block.brush.tipShape = BrushTipShape::Square;
        block.brush.antialiasing = false;
        block.brush.sizeDynamics = 0.0;
        block.brush.wobbleScale = 0.0;
        layer.strokes.append(block);

        const QImage selection = rectangularMask(
            document.size,
            QRect(8, 8, 32, 32));
        QTransform shift;
        shift.translate(64.0, 32.0);
        const std::optional<PixelSelectionOp> selectionOperation =
            makePixelSelectionOp(selection, shift, true, true);
        QVERIFY(selectionOperation.has_value());
        QCOMPARE(
            selectionOperation->sampling,
            SamplingMode::Nearest);
        Stroke operation;
        operation.mode = StrokeMode::PixelSelection;
        operation.pixelSelectionOp = *selectionOperation;
        layer.strokes.append(operation);

        Stroke overlay = makeStroke(
            StrokeMode::Paint,
            QColor(35, 180, 95),
            16.0,
            902,
            {QPointF(88.0, 56.0)});
        overlay.brush.tipShape = BrushTipShape::Square;
        overlay.brush.antialiasing = false;
        overlay.brush.sizeDynamics = 0.0;
        overlay.brush.wobbleScale = 0.0;
        layer.strokes.append(overlay);

        const QSize outputSize(32, 24);
        RenderEngine::ScaledRenderStats stats;
        const QImage preview = RenderEngine::renderScaled(
            document,
            0,
            outputSize,
            RenderEngine::ScaledRenderMode::DisplayPreview,
            &stats);
        const QImage exact = RenderEngine::renderScaled(
            document,
            0,
            outputSize,
            RenderEngine::ScaledRenderMode::NativeExact);
        QVERIFY(!preview.isNull());
        QVERIFY(!exact.isNull());
        QCOMPARE(preview, exact);
        QVERIFY(stats.usedDisplayScaleReplay);
        QVERIFY(stats.packedSelectionSamples > 0);
    }

    void conjugatesSelectionTransformForNonUniformPreviewScale()
    {
        Document document = Document::createDefault(QSize(128, 96));
        document.background = Qt::transparent;
        document.wobbleAmount = 0.0;
        Layer &layer = document.layers.first();
        layer.initialCanvasSize = document.size;

        Stroke block = makeStroke(
            StrokeMode::Paint,
            QColor(195, 65, 215),
            32.0,
            903,
            {QPointF(32.0, 32.0)});
        block.brush.tipShape = BrushTipShape::Square;
        block.brush.antialiasing = false;
        block.brush.sizeDynamics = 0.0;
        block.brush.wobbleScale = 0.0;
        layer.strokes.append(block);

        const QImage selection = rectangularMask(
            document.size,
            QRect(16, 16, 32, 32));
        // x' = 96 - y, y' = x: an integral quarter turn whose display
        // transform is anisotropic at the requested 1/4 x 1/8 scale.
        const QTransform quarterTurn(
            0.0, 1.0, 0.0,
            -1.0, 0.0, 0.0,
            96.0, 0.0, 1.0);
        const std::optional<PixelSelectionOp> pixelOperation =
            makePixelSelectionOp(
                selection,
                quarterTurn,
                true,
                true);
        QVERIFY(pixelOperation.has_value());
        QCOMPARE(pixelOperation->sampling, SamplingMode::Nearest);
        Stroke operation;
        operation.mode = StrokeMode::PixelSelection;
        operation.pixelSelectionOp = *pixelOperation;
        layer.strokes.append(operation);

        const QSize outputSize(32, 12);
        const QImage preview = RenderEngine::renderScaled(
            document,
            0,
            outputSize);
        const QImage exact = RenderEngine::renderScaled(
            document,
            0,
            outputSize,
            RenderEngine::ScaledRenderMode::NativeExact);
        QVERIFY(!preview.isNull());
        QCOMPARE(preview, exact);
    }

    void matchesNativeExactForRandomIntegralSelectionReplays()
    {
        quint32 random = 0x8f3a91d7U;
        const auto next = [&random]() {
            random ^= random << 13U;
            random ^= random >> 17U;
            random ^= random << 5U;
            return random;
        };

        for (int caseIndex = 0; caseIndex < 24; ++caseIndex) {
            Document document = Document::createDefault(QSize(128, 96));
            document.background = Qt::transparent;
            document.wobbleAmount = 0.0;
            Layer &layer = document.layers.first();
            layer.initialCanvasSize = document.size;

            const int blockEdge = 16 + static_cast<int>(next() % 3) * 8;
            const int sourceX =
                8 + static_cast<int>(next() % 9) * 4;
            const int sourceY =
                8 + static_cast<int>(next() % 7) * 4;
            Stroke block = makeStroke(
                StrokeMode::Paint,
                QColor(
                    40 + static_cast<int>(next() % 180),
                    40 + static_cast<int>(next() % 180),
                    40 + static_cast<int>(next() % 180)),
                blockEdge,
                next(),
                {QPointF(
                    sourceX + blockEdge * 0.5,
                    sourceY + blockEdge * 0.5)});
            block.brush.tipShape = BrushTipShape::Square;
            block.brush.antialiasing = false;
            block.brush.sizeDynamics = 0.0;
            block.brush.wobbleScale = 0.0;
            layer.strokes.append(block);

            const QImage selection = rectangularMask(
                document.size,
                QRect(sourceX, sourceY, blockEdge, blockEdge));
            const int deltaX =
                48 + static_cast<int>(next() % 5) * 4;
            const int deltaY =
                (static_cast<int>(next() % 7) - 3) * 4;
            QTransform transform;
            transform.translate(deltaX, deltaY);
            const bool clearSource = caseIndex % 3 != 1;
            const bool drawDestination = caseIndex % 3 != 2;
            const std::optional<PixelSelectionOp> pixelOperation =
                makePixelSelectionOp(
                    selection,
                    transform,
                    clearSource,
                    drawDestination);
            QVERIFY(pixelOperation.has_value());
            QCOMPARE(pixelOperation->sampling, SamplingMode::Nearest);
            Stroke operation;
            operation.mode = StrokeMode::PixelSelection;
            operation.pixelSelectionOp = *pixelOperation;
            layer.strokes.append(operation);

            const QSize outputSize(32, 24);
            const QImage preview = RenderEngine::renderScaled(
                document,
                caseIndex,
                outputSize);
            const QImage exact = RenderEngine::renderScaled(
                document,
                caseIndex,
                outputSize,
                RenderEngine::ScaledRenderMode::NativeExact);
            QVERIFY2(
                preview == exact,
                qPrintable(QStringLiteral(
                    "integral replay case %1 diverged")
                    .arg(caseIndex)));
        }
    }

    void replaysCanvasCropAndExpandWithoutNativeSurfaces()
    {
        Document document = Document::createDefault(QSize(128, 96));
        document.background = Qt::transparent;
        document.wobbleAmount = 0.0;
        Layer &layer = document.layers.first();
        layer.initialCanvasSize = document.size;

        Stroke first = makeStroke(
            StrokeMode::Paint,
            QColor(220, 65, 35),
            32.0,
            911,
            {QPointF(24.0, 28.0)});
        first.brush.tipShape = BrushTipShape::Square;
        first.brush.antialiasing = false;
        first.brush.sizeDynamics = 0.0;
        first.brush.wobbleScale = 0.0;
        layer.strokes.append(first);

        Stroke crop;
        crop.mode = StrokeMode::Reframe;
        crop.reframeOp = ReframeOp{
            ReframeMode::Canvas,
            SamplingMode::Nearest,
            QSize(128, 96),
            QSize(96, 72),
            QPoint(-16, -8)
        };
        layer.strokes.append(crop);

        Stroke second = makeStroke(
            StrokeMode::Paint,
            QColor(35, 165, 220),
            24.0,
            912,
            {QPointF(68.0, 48.0)});
        second.brush.tipShape = BrushTipShape::Square;
        second.brush.antialiasing = false;
        second.brush.sizeDynamics = 0.0;
        second.brush.wobbleScale = 0.0;
        layer.strokes.append(second);

        Stroke expand;
        expand.mode = StrokeMode::Reframe;
        expand.reframeOp = ReframeOp{
            ReframeMode::Canvas,
            SamplingMode::Nearest,
            QSize(96, 72),
            QSize(128, 96),
            QPoint(12, 8)
        };
        layer.strokes.append(expand);

        const QSize outputSize(32, 24);
        RenderEngine::ScaledRenderStats stats;
        const QImage preview = RenderEngine::renderScaled(
            document,
            0,
            outputSize,
            RenderEngine::ScaledRenderMode::DisplayPreview,
            &stats);
        const QImage exact = RenderEngine::renderScaled(
            document,
            0,
            outputSize,
            RenderEngine::ScaledRenderMode::NativeExact);
        QVERIFY(!preview.isNull());
        QCOMPARE(preview, exact);
        QVERIFY(stats.usedDisplayScaleReplay);
        QCOMPARE(stats.largestIntermediateImageSize, outputSize);
        QVERIFY(
            stats.largestIntermediateImageBytes
            <= static_cast<quint64>(outputSize.width())
                * outputSize.height() * sizeof(QRgb));
        // The left part clipped by the crop must not reappear on expansion.
        QCOMPARE(preview.pixelColor(1, 4), QColor(Qt::transparent));
    }

    void previewsSmoothImageResizeNearNativeExactResult()
    {
        Document document = Document::createDefault(QSize(96, 72));
        document.background = Qt::transparent;
        document.wobbleAmount = 0.0;
        Layer &layer = document.layers.first();
        layer.initialCanvasSize = QSize(160, 120);

        Stroke block = makeStroke(
            StrokeMode::Paint,
            QColor(45, 115, 225),
            56.0,
            921,
            {QPointF(52.0, 48.0)});
        block.brush.tipShape = BrushTipShape::Square;
        block.brush.antialiasing = false;
        block.brush.sizeDynamics = 0.0;
        block.brush.wobbleScale = 0.0;
        layer.strokes.append(block);

        Stroke circle = makeStroke(
            StrokeMode::Paint,
            QColor(235, 165, 35, 210),
            42.0,
            922,
            {QPointF(112.0, 74.0)});
        circle.brush.antialiasing = true;
        circle.brush.sizeDynamics = 0.0;
        circle.brush.wobbleScale = 0.0;
        layer.strokes.append(circle);

        Stroke resize;
        resize.mode = StrokeMode::Reframe;
        resize.reframeOp = ReframeOp{
            ReframeMode::Image,
            SamplingMode::Smooth,
            QSize(160, 120),
            document.size,
            QPoint()
        };
        layer.strokes.append(resize);

        const QSize outputSize(24, 18);
        RenderEngine::ScaledRenderStats stats;
        const QImage preview = RenderEngine::renderScaled(
            document,
            0,
            outputSize,
            RenderEngine::ScaledRenderMode::DisplayPreview,
            &stats);
        const QImage exact = RenderEngine::renderScaled(
            document,
            0,
            outputSize,
            RenderEngine::ScaledRenderMode::NativeExact);
        QVERIFY(!preview.isNull());
        QVERIFY(!exact.isNull());
        const ImageDifference difference =
            imageDifference(preview, exact);
        QVERIFY(difference.comparedChannels > 0);
        const qreal meanChannelDifference =
            static_cast<qreal>(difference.channelDifference)
            / difference.comparedChannels;
        QVERIFY2(
            meanChannelDifference < 8.0,
            qPrintable(QStringLiteral("mean channel difference: %1")
                .arg(meanChannelDifference)));
        QVERIFY(
            difference.visiblyDifferentPixels
            < outputSize.width() * outputSize.height() / 5);
        QVERIFY(stats.usedDisplayScaleReplay);
        QCOMPARE(stats.largestIntermediateImageSize, QSize(40, 30));
        QVERIFY(
            stats.largestIntermediateImageBytes
            < static_cast<quint64>(160) * 120 * sizeof(QRgb));
    }

    void previewsAnimatedSelectionTransformInOperationOrder()
    {
        Document document = Document::createDefault(QSize(160, 120));
        document.background = Qt::transparent;
        document.animationFrames = 8;
        document.wobbleAmount = 7.0;
        Layer &layer = document.layers.first();
        layer.initialCanvasSize = document.size;

        Stroke animated = makeStroke(
            StrokeMode::Paint,
            QColor(205, 55, 120),
            18.0,
            931,
            {
                QPointF(20.0, 72.0),
                QPointF(54.0, 28.0),
                QPointF(92.0, 76.0),
                QPointF(124.0, 32.0)
            });
        animated.brush.antialiasing = true;
        animated.brush.animatedJitter = true;
        layer.strokes.append(animated);

        const QImage selection = rectangularMask(
            document.size,
            QRect(8, 12, 132, 84));
        QTransform transform;
        transform.translate(76.0, 58.0);
        transform.rotate(9.0);
        transform.translate(-68.0, -54.0);
        const std::optional<PixelSelectionOp> pixelOperation =
            makePixelSelectionOp(selection, transform, true, true);
        QVERIFY(pixelOperation.has_value());
        QCOMPARE(pixelOperation->sampling, SamplingMode::Smooth);
        Stroke operation;
        operation.mode = StrokeMode::PixelSelection;
        operation.pixelSelectionOp = *pixelOperation;
        layer.strokes.append(operation);

        Stroke overlay = makeStroke(
            StrokeMode::Paint,
            QColor(35, 190, 115),
            10.0,
            932,
            {QPointF(24.0, 18.0), QPointF(132.0, 102.0)});
        overlay.brush.antialiasing = true;
        overlay.brush.wobbleScale = 0.0;
        layer.strokes.append(overlay);

        const QSize outputSize(40, 30);
        QVector<QImage> previews;
        for (const int frame : {0, 1, 5}) {
            const QImage preview = RenderEngine::renderScaled(
                document,
                frame,
                outputSize);
            const QImage exact = RenderEngine::renderScaled(
                document,
                frame,
                outputSize,
                RenderEngine::ScaledRenderMode::NativeExact);
            QVERIFY(!preview.isNull());
            QVERIFY(!exact.isNull());
            const ImageDifference difference =
                imageDifference(preview, exact, 36);
            const qreal meanChannelDifference =
                static_cast<qreal>(difference.channelDifference)
                / difference.comparedChannels;
            QVERIFY2(
                meanChannelDifference < 12.0,
                qPrintable(QStringLiteral(
                    "frame %1 mean channel difference: %2")
                    .arg(frame)
                    .arg(meanChannelDifference)));
            QVERIFY(
                difference.visiblyDifferentPixels
                < outputSize.width() * outputSize.height() / 4);
            previews.append(preview);
        }
        QVERIFY(previews[0] != previews[1]);
        QVERIFY(previews[1] != previews[2]);
    }

    void composesPreviewLayerSplitWithFramebufferOperations()
    {
        Document document = Document::createDefault(QSize(128, 96));
        document.background = QColor(248, 245, 238);
        document.wobbleAmount = 0.0;
        Layer &bottom = document.layers.first();
        bottom.initialCanvasSize = document.size;
        Stroke bottomStroke = makeStroke(
            StrokeMode::Paint,
            QColor(70, 110, 210),
            28.0,
            941,
            {QPointF(28.0, 28.0)});
        bottomStroke.brush.tipShape = BrushTipShape::Square;
        bottomStroke.brush.sizeDynamics = 0.0;
        bottom.strokes.append(bottomStroke);

        Layer middle;
        middle.name = QStringLiteral("operation layer");
        middle.initialCanvasSize = document.size;
        Stroke middleStroke = makeStroke(
            StrokeMode::Paint,
            QColor(220, 70, 50),
            32.0,
            942,
            {QPointF(36.0, 52.0)});
        middleStroke.brush.tipShape = BrushTipShape::Square;
        middleStroke.brush.sizeDynamics = 0.0;
        middle.strokes.append(middleStroke);
        const QImage selection = rectangularMask(
            document.size,
            QRect(20, 36, 32, 32));
        QTransform shift;
        shift.translate(52.0, 8.0);
        const std::optional<PixelSelectionOp> pixelOperation =
            makePixelSelectionOp(selection, shift, true, true);
        QVERIFY(pixelOperation.has_value());
        Stroke operation;
        operation.mode = StrokeMode::PixelSelection;
        operation.pixelSelectionOp = *pixelOperation;
        middle.strokes.append(operation);
        const QUuid middleId = middle.id;
        document.layers.append(middle);

        Layer top;
        top.name = QStringLiteral("top");
        top.initialCanvasSize = document.size;
        top.strokes.append(makeStroke(
            StrokeMode::Paint,
            QColor(30, 180, 95),
            12.0,
            943,
            {QPointF(12.0, 84.0), QPointF(116.0, 12.0)}));
        document.layers.append(top);

        const QSize outputSize(32, 24);
        RenderEngine::ScaledRenderStats stats;
        const RenderEngine::LayerSplitFrame split =
            RenderEngine::renderLayerSplit(
                document,
                0,
                outputSize,
                middleId,
                RenderEngine::ScaledRenderMode::DisplayPreview,
                &stats);
        QVERIFY(split.valid);
        const QImage composed =
            RenderEngine::composeLayerSplit(split, split.layerBase);
        const QImage full = RenderEngine::renderScaled(
            document,
            0,
            outputSize);
        QCOMPARE(composed, full);
        QVERIFY(stats.usedDisplayScaleReplay);
        QVERIFY(stats.packedSelectionSamples > 0);
        QVERIFY(
            stats.largestIntermediateImageBytes
            <= static_cast<quint64>(outputSize.width())
                * outputSize.height() * sizeof(QRgb));
        const quint64 frameBytes =
            static_cast<quint64>(outputSize.width())
            * outputSize.height() * sizeof(QRgb);
        QVERIFY(
            stats.maximumEstimatedWorkingSetBytes
            <= frameBytes * 4);
    }

    void replaysPendingSelectionOnCachedLayerFramebuffer()
    {
        Document document = Document::createDefault(QSize(128, 96));
        document.background = QColor(248, 245, 238);
        document.wobbleAmount = 0.0;
        Layer &target = document.layers.first();
        const QUuid targetId = target.id;
        target.initialCanvasSize = document.size;
        Stroke block = makeStroke(
            StrokeMode::Paint,
            QColor(45, 105, 225),
            32.0,
            944,
            {QPointF(32.0, 48.0)});
        block.brush.tipShape = BrushTipShape::Square;
        block.brush.antialiasing = false;
        block.brush.sizeDynamics = 0.0;
        block.brush.wobbleScale = 0.0;
        target.strokes.append(block);

        Layer top;
        top.name = QStringLiteral("top");
        top.initialCanvasSize = document.size;
        top.strokes.append(makeStroke(
            StrokeMode::Paint,
            QColor(220, 60, 70),
            10.0,
            945,
            {QPointF(8.0, 88.0), QPointF(120.0, 8.0)}));
        document.layers.append(top);

        const QImage selection = rectangularMask(
            document.size,
            QRect(16, 32, 32, 32));
        QTransform shift;
        shift.translate(64.0, 8.0);
        const std::optional<PixelSelectionOp> operation =
            makePixelSelectionOp(selection, shift, true, true);
        QVERIFY(operation.has_value());

        const QSize outputSize(32, 24);
        const RenderEngine::LayerSplitFrame split =
            RenderEngine::renderLayerSplit(
                document,
                0,
                outputSize,
                targetId);
        QVERIFY(split.valid);
        const QImage cachedBase = split.layerBase;
        QImage previewLayer = split.layerBase;
        RenderEngine::ScaledRenderStats previewStats;
        QVERIFY(RenderEngine::replayPixelSelectionOnLayer(
            previewLayer,
            *operation,
            RenderEngine::ScaledRenderMode::DisplayPreview,
            &previewStats));
        QCOMPARE(split.layerBase, cachedBase);
        QVERIFY(previewStats.usedDisplayScaleReplay);
        QVERIFY(!previewStats.usedNativeExactFallback);
        QCOMPARE(previewStats.primitiveStrokesRendered, quint64(0));
        QCOMPARE(
            previewStats.pixelSelectionOperationsReplayed,
            quint64(1));
        QVERIFY(previewStats.packedSelectionSamples > 0);

        Document expectedPreview = document;
        Stroke previewOperation;
        previewOperation.mode = StrokeMode::PixelSelection;
        previewOperation.pixelSelectionOp = *operation;
        expectedPreview.layers.first().strokes.append(previewOperation);
        QCOMPARE(
            RenderEngine::composeLayerSplit(split, previewLayer),
            RenderEngine::renderScaled(
                expectedPreview,
                0,
                outputSize));

        const RenderEngine::LayerSplitFrame nativeSplit =
            RenderEngine::renderLayerSplit(
                document,
                0,
                document.size,
                targetId);
        QVERIFY(nativeSplit.valid);
        QImage nativeLayer = nativeSplit.layerBase;
        RenderEngine::ScaledRenderStats nativeStats;
        QVERIFY(RenderEngine::replayPixelSelectionOnLayer(
            nativeLayer,
            *operation,
            RenderEngine::ScaledRenderMode::NativeExact,
            &nativeStats));
        QVERIFY(!nativeStats.usedDisplayScaleReplay);
        QVERIFY(nativeStats.usedNativeExactFallback);
        QCOMPARE(nativeStats.primitiveStrokesRendered, quint64(0));
        QCOMPARE(
            nativeStats.pixelSelectionOperationsReplayed,
            quint64(1));
        QCOMPARE(
            RenderEngine::composeLayerSplit(nativeSplit, nativeLayer),
            RenderEngine::render(expectedPreview, 0));
    }

    void replaysPendingSelectionWhenOnlyOnePreviewAxisShrinks()
    {
        const auto verify = [](const QSize &canvasSize,
                               const QSize &previewSize,
                               const QRect &selectionBounds,
                               const QPointF &translation) {
            Document document = Document::createDefault(canvasSize);
            document.background = Qt::transparent;
            document.wobbleAmount = 0.0;
            Layer &layer = document.layers.first();
            layer.initialCanvasSize = canvasSize;

            Stroke source = makeStroke(
                StrokeMode::Paint,
                QColor(45, 105, 225),
                1.0,
                946,
                {QPointF(selectionBounds.center())});
            source.brush.tipShape = BrushTipShape::Square;
            source.brush.antialiasing = false;
            source.brush.sizeDynamics = 0.0;
            source.brush.wobbleScale = 0.0;
            layer.strokes.append(source);

            const QImage selection = rectangularMask(
                canvasSize,
                selectionBounds);
            QTransform transform;
            transform.translate(
                translation.x(),
                translation.y());
            const std::optional<PixelSelectionOp> operation =
                makePixelSelectionOp(selection, transform, true, true);
            QVERIFY(operation.has_value());

            RenderEngine::ScaledRenderStats splitStats;
            const RenderEngine::LayerSplitFrame split =
                RenderEngine::renderLayerSplit(
                    document,
                    0,
                    previewSize,
                    layer.id,
                    RenderEngine::ScaledRenderMode::DisplayPreview,
                    &splitStats);
            QVERIFY(split.valid);
            QVERIFY(splitStats.usedDisplayScaleReplay);
            QVERIFY(!splitStats.usedNativeExactFallback);

            QImage previewLayer = split.layerBase;
            RenderEngine::ScaledRenderStats replayStats;
            QVERIFY(RenderEngine::replayPixelSelectionOnLayer(
                previewLayer,
                *operation,
                RenderEngine::ScaledRenderMode::DisplayPreview,
                &replayStats));
            QVERIFY(replayStats.usedDisplayScaleReplay);
            QVERIFY(!replayStats.usedNativeExactFallback);
            QCOMPARE(replayStats.primitiveStrokesRendered, quint64(0));
            QCOMPARE(
                replayStats.pixelSelectionOperationsReplayed,
                quint64(1));

            Document expected = document;
            Stroke selectionOperation;
            selectionOperation.mode = StrokeMode::PixelSelection;
            selectionOperation.pixelSelectionOp = *operation;
            expected.layers.first().strokes.append(selectionOperation);
            QCOMPARE(
                RenderEngine::composeLayerSplit(split, previewLayer),
                RenderEngine::renderScaled(
                    expected,
                    0,
                    previewSize,
                    RenderEngine::ScaledRenderMode::DisplayPreview));
        };

        verify(
            QSize(4096, 1),
            QSize(410, 1),
            QRect(512, 0, 1024, 1),
            QPointF(1024.0, 0.0));
        verify(
            QSize(1, 4096),
            QSize(1, 410),
            QRect(0, 512, 1, 1024),
            QPointF(0.0, 1024.0));
    }

    void cachedSelectionReplayDoesNotWalkTwentyThousandStrokes()
    {
        Document document = Document::createDefault(QSize(128, 128));
        document.background = Qt::transparent;
        document.wobbleAmount = 0.0;
        Layer &layer = document.layers.first();
        layer.initialCanvasSize = document.size;
        layer.strokes.reserve(DocumentLimits::maximumStrokesPerLayer);
        for (int index = 0;
             index < DocumentLimits::maximumStrokesPerLayer;
             ++index) {
            Stroke dot = makeStroke(
                StrokeMode::Paint,
                QColor(35, 95, 225, 24),
                1.0,
                static_cast<quint64>(index + 1),
                {QPointF(
                    32.0 + index % 64,
                    32.0 + (index / 64) % 64)});
            dot.brush.antialiasing = false;
            dot.brush.sizeDynamics = 0.0;
            dot.brush.wobbleScale = 0.0;
            layer.strokes.append(std::move(dot));
        }

        const QSize outputSize(32, 32);
        RenderEngine::ScaledRenderStats splitStats;
        const RenderEngine::LayerSplitFrame split =
            RenderEngine::renderLayerSplit(
                document,
                0,
                outputSize,
                layer.id,
                RenderEngine::ScaledRenderMode::DisplayPreview,
                &splitStats);
        QVERIFY(split.valid);
        QCOMPARE(
            splitStats.primitiveStrokesRendered,
            static_cast<quint64>(
                DocumentLimits::maximumStrokesPerLayer));

        const QImage selection = rectangularMask(
            document.size,
            QRect(24, 24, 80, 80));
        QTransform shift;
        shift.translate(8.0, 0.0);
        const std::optional<PixelSelectionOp> operation =
            makePixelSelectionOp(selection, shift, true, true);
        QVERIFY(operation.has_value());
        const QImage cachedBase = split.layerBase;
        for (int preview = 0; preview < 4; ++preview) {
            QImage layerPreview = split.layerBase;
            RenderEngine::ScaledRenderStats replayStats;
            QVERIFY(RenderEngine::replayPixelSelectionOnLayer(
                layerPreview,
                *operation,
                RenderEngine::ScaledRenderMode::DisplayPreview,
                &replayStats));
            QCOMPARE(replayStats.primitiveStrokesRendered, quint64(0));
            QCOMPARE(
                replayStats.pixelSelectionOperationsReplayed,
                quint64(1));
            QVERIFY(replayStats.packedSelectionSamples > 0);
            QCOMPARE(split.layerBase, cachedBase);
        }
    }

    void fourKOperationPreviewNeverAllocatesNativeFramebuffer()
    {
        constexpr int edge = DocumentLimits::maximumCanvasEdge;
        const QSize canvasSize(edge, edge);
        Document document = Document::createDefault(canvasSize);
        document.background = Qt::transparent;
        document.wobbleAmount = 0.0;
        Layer &layer = document.layers.first();
        layer.initialCanvasSize = canvasSize;

        Stroke block = makeStroke(
            StrokeMode::Paint,
            QColor(65, 125, 230),
            512.0,
            951,
            {QPointF(edge / 2.0, edge / 2.0)});
        block.brush.tipShape = BrushTipShape::Square;
        block.brush.antialiasing = false;
        block.brush.sizeDynamics = 0.0;
        block.brush.wobbleScale = 0.0;
        layer.strokes.append(block);

        PixelSelectionOp pixelOperation;
        pixelOperation.canvasSize = canvasSize;
        pixelOperation.sourceBounds = QRect(QPoint(), canvasSize);
        const qsizetype packedStride =
            (static_cast<qsizetype>(edge) + 7) / 8;
        pixelOperation.packedMask = QByteArray(
            packedStride * edge,
            static_cast<char>(0xff));
        pixelOperation.transform.translate(64.0, 0.0);
        pixelOperation.sampling = SamplingMode::Nearest;
        pixelOperation.clearSource = true;
        pixelOperation.drawDestination = true;
        QVERIFY(isValidPixelSelectionOp(pixelOperation));
        Stroke operation;
        operation.mode = StrokeMode::PixelSelection;
        operation.pixelSelectionOp = pixelOperation;
        layer.strokes.append(operation);

        const QSize outputSize(256, 256);
        RenderEngine::ScaledRenderStats stats;
        const QImage preview = RenderEngine::renderScaled(
            document,
            0,
            outputSize,
            RenderEngine::ScaledRenderMode::DisplayPreview,
            &stats);
        QVERIFY(!preview.isNull());
        QCOMPARE(preview.size(), outputSize);
        QVERIFY(stats.usedDisplayScaleReplay);
        QVERIFY(!stats.usedNativeExactFallback);
        QCOMPARE(stats.largestIntermediateImageSize, outputSize);
        const quint64 displayFrameBytes =
            static_cast<quint64>(outputSize.width())
            * outputSize.height() * sizeof(QRgb);
        const quint64 nativeFrameBytes =
            static_cast<quint64>(edge) * edge * sizeof(QRgb);
        QCOMPARE(stats.largestIntermediateImageBytes, displayFrameBytes);
        QVERIFY(stats.maximumEstimatedWorkingSetBytes < nativeFrameBytes);
        QCOMPARE(
            stats.packedSelectionSamples,
            static_cast<quint64>(outputSize.width())
                * outputSize.height());
        QCOMPARE(
            pixelOperation.packedMask.size(),
            qsizetype(2 * 1024 * 1024));
    }

    void loopsAtFrameCount()
    {
        const Document document = animatedDocument();
        const QImage first = RenderEngine::render(document, 0);
        const QImage looped =
            RenderEngine::render(document, document.animationFrames);

        QVERIFY(first == looped);
    }

    void changesAcrossWobbleFrames()
    {
        const Document document = animatedDocument();
        const QImage first = RenderEngine::render(document, 0);
        const QImage second = RenderEngine::render(document, 1);

        QVERIFY(first != second);
    }

    void staysStillWhenWobbleIsZero()
    {
        Document document = animatedDocument();
        document.wobbleAmount = 0.0;
        document.layers.first().strokes.first().width = 40.0;

        const QImage first = RenderEngine::render(document, 0);
        QVERIFY(!first.isNull());
        for (int frame = 1; frame < document.animationFrames; ++frame) {
            QCOMPARE(RenderEngine::render(document, frame), first);
        }
    }

    void staysStillWhenBrushWobbleScaleIsZero()
    {
        Document document = animatedDocument();
        document.layers.first().strokes.first().brush.wobbleScale = 0.0;

        const QImage first = RenderEngine::render(document, 0);
        QVERIFY(!first.isNull());
        for (int frame = 1; frame < document.animationFrames; ++frame) {
            QCOMPARE(RenderEngine::render(document, frame), first);
        }
    }

    void scalesWobbleByBrushWobbleScale()
    {
        Document document = animatedDocument();
        const QImage normal = RenderEngine::render(document, 0);
        document.layers.first().strokes.first().brush.wobbleScale = 2.0;
        const QImage rough = RenderEngine::render(document, 0);

        QVERIFY(!normal.isNull());
        QVERIFY(normal != rough);
    }

    void composesLayerSplitLikeFullRender()
    {
        Document document = Document::createDefault(QSize(96, 72));
        document.animationFrames = 6;
        document.wobbleAmount = 3.0;
        document.layers.first().strokes.append(makeStroke(
            StrokeMode::Paint,
            QColor(200, 40, 40),
            8.0,
            11,
            {QPointF(10.0, 10.0), QPointF(80.0, 60.0)}));
        Layer middle;
        middle.name = QStringLiteral("Middle");
        middle.strokes.append(makeStroke(
            StrokeMode::Paint,
            QColor(40, 200, 40),
            7.0,
            22,
            {QPointF(10.0, 60.0), QPointF(80.0, 10.0)}));
        document.layers.append(middle);
        Layer top;
        top.name = QStringLiteral("Top");
        top.opacity = 0.65;
        top.strokes.append(makeStroke(
            StrokeMode::Paint,
            QColor(40, 40, 200),
            9.0,
            33,
            {QPointF(48.0, 6.0), QPointF(48.0, 66.0)}));
        document.layers.append(top);
        const QUuid middleId = document.layers[1].id;

        const QVector<Stroke> activeStrokes = {
            makeStroke(
                StrokeMode::Paint,
                QColor(250, 210, 60),
                5.0,
                44,
                {QPointF(20.0, 20.0), QPointF(70.0, 50.0)}),
            makeStroke(
                StrokeMode::Erase,
                QColor(Qt::black),
                12.0,
                55,
                {QPointF(30.0, 50.0), QPointF(60.0, 20.0)})
        };
        for (const Stroke &active : activeStrokes) {
            Document full = document;
            full.layer(middleId)->strokes.append(active);
            const QImage expected = RenderEngine::render(full, 2);

            const RenderEngine::LayerSplitFrame split =
                RenderEngine::renderLayerSplit(
                    document,
                    2,
                    document.size,
                    middleId);
            QVERIFY(split.valid);
            QImage layerImage = split.layerBase;
            QVERIFY(RenderEngine::renderStrokesOnLayer(
                layerImage,
                document,
                {active},
                2,
                document.size));
            const QImage actual =
                RenderEngine::composeLayerSplit(split, layerImage);

            QCOMPARE(actual.size(), expected.size());
            for (int y = 0; y < expected.height(); ++y) {
                for (int x = 0; x < expected.width(); ++x) {
                    const QColor a = actual.pixelColor(x, y);
                    const QColor b = expected.pixelColor(x, y);
                    QVERIFY(std::abs(a.red() - b.red()) <= 2);
                    QVERIFY(std::abs(a.green() - b.green()) <= 2);
                    QVERIFY(std::abs(a.blue() - b.blue()) <= 2);
                    QVERIFY(std::abs(a.alpha() - b.alpha()) <= 2);
                }
            }
        }
    }

    void displacesWobbleLinearlyWithAmount()
    {
        QVector<QPointF> positions;
        for (int index = 0; index <= 20; ++index) {
            positions.append(QPointF(10.0 + index * 9.0, 60.0));
        }
        const Stroke stroke = makeStroke(
            StrokeMode::Paint,
            QColor(10, 20, 30),
            6.0,
            0xfeedbeefULL,
            positions);

        const QPainterPath still =
            RenderEngine::strokePath(stroke, 3, 12, 0.0);
        const QPainterPath single =
            RenderEngine::strokePath(stroke, 3, 12, 2.0);
        const QPainterPath doubled =
            RenderEngine::strokePath(stroke, 3, 12, 4.0);

        QCOMPARE(single.elementCount(), still.elementCount());
        QCOMPARE(doubled.elementCount(), still.elementCount());
        qreal largestOffset = 0.0;
        for (int index = 0; index < still.elementCount(); ++index) {
            const QPointF base(
                still.elementAt(index).x,
                still.elementAt(index).y);
            const QPointF offsetSingle =
                QPointF(single.elementAt(index).x, single.elementAt(index).y)
                - base;
            const QPointF offsetDouble =
                QPointF(doubled.elementAt(index).x, doubled.elementAt(index).y)
                - base;
            largestOffset = std::max(
                largestOffset,
                std::hypot(offsetSingle.x(), offsetSingle.y()));
            QVERIFY(
                std::abs(offsetDouble.x() - offsetSingle.x() * 2.0) < 1e-6);
            QVERIFY(
                std::abs(offsetDouble.y() - offsetSingle.y() * 2.0) < 1e-6);
        }
        QVERIFY(largestOffset > 0.05);
    }

    void rendersCrispPixelEdges()
    {
        Document document = Document::createDefault(QSize(64, 64));
        document.wobbleAmount = 0.0;
        const QColor strokeColor(255, 120, 120);
        document.layers.first().strokes.append(makeStroke(
            StrokeMode::Paint,
            strokeColor,
            3.0,
            45,
            {
                QPointF(7.25, 51.75),
                QPointF(22.5, 11.25),
                QPointF(55.75, 44.5)
            }));

        const QImage image = RenderEngine::render(document, 0);
        QVERIFY(!image.isNull());
        int paintedPixels = 0;
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                const QColor pixel = image.pixelColor(x, y);
                QVERIFY(
                    pixel == document.background
                    || pixel == strokeColor);
                if (pixel == strokeColor) {
                    ++paintedPixels;
                }
            }
        }
        QVERIFY(paintedPixels > 0);
    }

    void rendersSmoothEdgesWithAntialiasing()
    {
        Document document = Document::createDefault(QSize(64, 64));
        document.wobbleAmount = 0.0;
        const QColor strokeColor(255, 120, 120);
        Stroke stroke = makeStroke(
            StrokeMode::Paint,
            strokeColor,
            3.0,
            45,
            {
                QPointF(7.25, 51.75),
                QPointF(22.5, 11.25),
                QPointF(55.75, 44.5)
            });
        stroke.brush.antialiasing = true;
        document.layers.first().strokes.append(stroke);

        const QImage image = RenderEngine::render(document, 0);
        QVERIFY(!image.isNull());
        int blendedPixels = 0;
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                const QColor pixel = image.pixelColor(x, y);
                if (pixel != document.background
                    && pixel != strokeColor) {
                    ++blendedPixels;
                }
            }
        }
        QVERIFY(blendedPixels > 0);
    }

    void rejectsUnallocatableCanvas()
    {
        Document document = Document::createDefault(QSize(
            std::numeric_limits<int>::max(),
            std::numeric_limits<int>::max()));
        QVERIFY(RenderEngine::render(document, 0).isNull());
    }

    void ignoresUnsafeStrokeCoordinates()
    {
        Document document = Document::createDefault(QSize(64, 64));
        Stroke stroke;
        stroke.points = {
            {
                QPointF(
                    std::numeric_limits<qreal>::infinity(),
                    16.0),
                1.0
            }
        };
        document.layers.first().strokes.append(stroke);

        const QImage image = RenderEngine::render(document, 0);
        QVERIFY(!image.isNull());
        QCOMPARE(image.pixelColor(16, 16), QColor(Qt::white));
        QVERIFY(RenderEngine::strokePath(
            stroke,
            0,
            document.animationFrames,
            document.wobbleAmount).isEmpty());
    }

    void keepsErasersLocalToTheirLayer()
    {
        Document document;
        document.size = QSize(80, 64);
        document.background = QColor(10, 20, 30);
        document.animationFrames = 8;
        document.wobbleAmount = 0.0;

        Layer bottom;
        bottom.name = QStringLiteral("Bottom");
        bottom.strokes.append(makeStroke(
            StrokeMode::Paint,
            QColor(20, 180, 80),
            20.0,
            10,
            {QPointF(8.0, 32.0), QPointF(72.0, 32.0)}));

        Layer middle;
        middle.name = QStringLiteral("Middle");
        middle.strokes.append(makeStroke(
            StrokeMode::Paint,
            QColor(220, 40, 50),
            20.0,
            20,
            {QPointF(8.0, 32.0), QPointF(72.0, 32.0)}));
        middle.strokes.append(makeStroke(
            StrokeMode::Erase,
            Qt::black,
            14.0,
            30,
            {QPointF(48.0, 32.0)}));

        Layer top;
        top.name = QStringLiteral("Top");
        top.strokes.append(makeStroke(
            StrokeMode::Erase,
            Qt::black,
            14.0,
            40,
            {QPointF(20.0, 32.0)}));

        document.layers = {bottom, middle, top};
        document.activeLayerId = top.id;

        const QImage image = RenderEngine::render(document, 0);
        QCOMPARE(image.pixelColor(48, 32), QColor(20, 180, 80));
        QCOMPARE(image.pixelColor(20, 32), QColor(220, 40, 50));
    }

    void doesNotFillOpenStrokes()
    {
        Document document = Document::createDefault(QSize(64, 64));
        document.wobbleAmount = 0.0;
        document.layers.first().strokes.append(makeStroke(
            StrokeMode::Paint,
            Qt::black,
            4.0,
            50,
            {
                QPointF(8.0, 52.0),
                QPointF(32.0, 8.0),
                QPointF(56.0, 52.0)
            }));

        const QImage image = RenderEngine::render(document, 0);
        QCOMPARE(image.pixelColor(32, 46), QColor(Qt::white));
        QCOMPARE(image.pixelColor(32, 8), QColor(Qt::black));
    }

    void clipsPaintAndFillToSelectionMasks()
    {
        QImage clipMask(QSize(64, 64), QImage::Format_Grayscale8);
        clipMask.fill(0);
        for (int y = 12; y < 52; ++y) {
            std::fill_n(clipMask.scanLine(y) + 12, 20, 255);
        }

        Document paintDocument =
            Document::createDefault(QSize(64, 64));
        paintDocument.wobbleAmount = 0.0;
        Stroke paint = makeStroke(
            StrokeMode::Paint,
            QColor(220, 30, 40),
            16.0,
            60,
            {QPointF(4.0, 32.0), QPointF(60.0, 32.0)});
        paint.clipMask = clipMask;
        paintDocument.layers.first().strokes.append(paint);
        const QImage painted = RenderEngine::render(paintDocument, 0);
        QCOMPARE(painted.pixelColor(20, 32), paint.color);
        QCOMPARE(painted.pixelColor(45, 32), QColor(Qt::white));

        Document fillDocument =
            Document::createDefault(QSize(64, 64));
        Stroke fill;
        fill.mode = StrokeMode::Fill;
        fill.color = QColor(30, 80, 220);
        fill.points = {{QPointF(20.0, 32.0), 1.0}};
        fill.clipMask = clipMask;
        fillDocument.layers.first().strokes.append(fill);
        const QImage filled = RenderEngine::render(fillDocument, 0);
        QCOMPARE(filled.pixelColor(20, 32), fill.color);
        QCOMPARE(filled.pixelColor(45, 32), QColor(Qt::white));

        const QImage scaled =
            RenderEngine::renderScaled(fillDocument, 0, QSize(32, 32));
        QCOMPARE(scaled.pixelColor(10, 16), fill.color);
        QCOMPARE(scaled.pixelColor(22, 16), QColor(Qt::white));
    }

    void canvasCropPreservesDisconnectedFillPixels()
    {
        Document document = Document::createDefault(QSize(10, 10));
        document.wobbleAmount = 0.0;

        Stroke barrier = makeStroke(
            StrokeMode::Paint,
            Qt::black,
            1.0,
            61,
            {QPointF(5.0, 0.0), QPointF(5.0, 7.0)});
        barrier.brush.antialiasing = false;
        Stroke fill;
        fill.mode = StrokeMode::Fill;
        fill.color = QColor(30, 80, 220);
        fill.points = {{QPointF(4.0, 8.0), 1.0}};
        document.layers.first().strokes = {barrier, fill};

        const QImage before = RenderEngine::render(document, 0);
        QCOMPARE(before.pixelColor(2, 4), fill.color);
        QCOMPARE(before.pixelColor(8, 4), fill.color);

        DocumentController controller;
        controller.loadDocument(document);
        QVERIFY(controller.resizeCanvas(QSize(10, 8), QPoint()));

        const QImage cropped = RenderEngine::render(
            controller.document(),
            0);
        const QImage expected = before.copy(QRect(0, 0, 10, 8));
        QCOMPARE(cropped, expected);
    }

    void movesFlattenedPaintEraseSelectionOverExistingPixels()
    {
        Document document = Document::createDefault(QSize(96, 48));
        document.background = Qt::transparent;
        document.animationFrames = 4;
        document.wobbleAmount = 5.0;
        Stroke sourcePaint = makeStroke(
            StrokeMode::Paint,
            QColor(30, 90, 220),
            18.0,
            71,
            {QPointF(14.0, 24.0), QPointF(26.0, 24.0)});
        Stroke sourceErase = makeStroke(
            StrokeMode::Erase,
            Qt::black,
            8.0,
            72,
            {QPointF(20.0, 24.0)});
        Stroke destinationPaint = makeStroke(
            StrokeMode::Paint,
            QColor(220, 50, 40),
            18.0,
            73,
            {QPointF(54.0, 24.0), QPointF(66.0, 24.0)});
        sourcePaint.brush.antialiasing = false;
        sourceErase.brush.antialiasing = false;
        destinationPaint.brush.antialiasing = false;
        document.layers.first().strokes = {
            sourcePaint,
            sourceErase,
            destinationPaint
        };

        const QImage selection = rectangularMask(
            document.size,
            QRect(4, 10, 32, 29));
        const QPoint delta(40, 0);
        QVector<QImage> beforeFrames;
        QVector<QImage> expectedFrames;
        for (int frame = 0; frame < document.animationFrames; ++frame) {
            const QImage before =
                activeLayerPixels(document, frame);
            QVERIFY(!before.isNull());
            beforeFrames.append(before);
            expectedFrames.append(
                rasterSelectionResult(
                    before,
                    selection,
                    delta,
                    true));
            QVERIFY(!expectedFrames.last().isNull());
        }
        QVERIFY(beforeFrames[0] != beforeFrames[1]);

        DocumentController controller;
        controller.loadDocument(document);
        QVERIFY(controller.moveStrokes(
            document.activeLayerId,
            {sourcePaint.id, sourceErase.id},
            delta,
            selection));
        for (int frame = 0; frame < document.animationFrames; ++frame) {
            QCOMPARE(
                activeLayerPixels(controller.document(), frame),
                expectedFrames[frame]);
        }

        controller.undoStack()->undo();
        for (int frame = 0; frame < document.animationFrames; ++frame) {
            QCOMPARE(
                activeLayerPixels(controller.document(), frame),
                beforeFrames[frame]);
        }
        controller.undoStack()->redo();
        for (int frame = 0; frame < document.animationFrames; ++frame) {
            QCOMPARE(
                activeLayerPixels(controller.document(), frame),
                expectedFrames[frame]);
        }
    }

    void duplicatesFlattenedPaintEraseSelectionOverExistingPixels()
    {
        Document document = Document::createDefault(QSize(96, 48));
        document.background = Qt::transparent;
        document.wobbleAmount = 0.0;
        Stroke sourcePaint = makeStroke(
            StrokeMode::Paint,
            QColor(35, 170, 90),
            18.0,
            81,
            {QPointF(14.0, 24.0), QPointF(26.0, 24.0)});
        Stroke sourceErase = makeStroke(
            StrokeMode::Erase,
            Qt::black,
            8.0,
            82,
            {QPointF(20.0, 24.0)});
        Stroke destinationPaint = makeStroke(
            StrokeMode::Paint,
            QColor(230, 170, 35),
            18.0,
            83,
            {QPointF(54.0, 24.0), QPointF(66.0, 24.0)});
        sourcePaint.brush.antialiasing = false;
        sourceErase.brush.antialiasing = false;
        destinationPaint.brush.antialiasing = false;
        document.layers.first().strokes = {
            sourcePaint,
            sourceErase,
            destinationPaint
        };

        const QImage selection = rectangularMask(
            document.size,
            QRect(4, 10, 32, 29));
        const QPoint delta(40, 0);
        const QImage before = activeLayerPixels(document);
        const QImage expected =
            rasterSelectionResult(before, selection, delta, false);
        QVERIFY(!before.isNull());
        QVERIFY(!expected.isNull());

        DocumentController controller;
        controller.loadDocument(document);
        QVERIFY(controller.duplicateStrokes(
            document.activeLayerId,
            {sourcePaint.id, sourceErase.id},
            delta,
            selection));
        QCOMPARE(activeLayerPixels(controller.document()), expected);
        QCOMPARE(
            activeLayerPixels(controller.document()).pixelColor(60, 24),
            destinationPaint.color);

        controller.undoStack()->undo();
        QCOMPARE(activeLayerPixels(controller.document()), before);
        controller.undoStack()->redo();
        QCOMPARE(activeLayerPixels(controller.document()), expected);
    }

    void deletesFinalPaintEraseSelectionPixels()
    {
        Document document = Document::createDefault(QSize(96, 48));
        document.background = Qt::transparent;
        document.wobbleAmount = 0.0;
        Stroke sourcePaint = makeStroke(
            StrokeMode::Paint,
            QColor(40, 130, 225),
            18.0,
            86,
            {QPointF(14.0, 24.0), QPointF(26.0, 24.0)});
        Stroke sourceErase = makeStroke(
            StrokeMode::Erase,
            Qt::black,
            8.0,
            87,
            {QPointF(20.0, 24.0)});
        Stroke outsidePaint = makeStroke(
            StrokeMode::Paint,
            QColor(230, 80, 120),
            18.0,
            88,
            {QPointF(54.0, 24.0), QPointF(66.0, 24.0)});
        sourcePaint.brush.antialiasing = false;
        sourceErase.brush.antialiasing = false;
        outsidePaint.brush.antialiasing = false;
        document.layers.first().strokes = {
            sourcePaint,
            sourceErase,
            outsidePaint
        };

        const QImage selection = rectangularMask(
            document.size,
            QRect(4, 10, 32, 29));
        const QImage before = activeLayerPixels(document);
        const QImage expected =
            clearedSelectionResult(before, selection);
        QVERIFY(!before.isNull());
        QVERIFY(!expected.isNull());

        DocumentController controller;
        controller.loadDocument(document);
        QVERIFY(controller.removeSelectedContent(
            document.activeLayerId,
            {sourcePaint.id, sourceErase.id},
            selection));
        QCOMPARE(activeLayerPixels(controller.document()), expected);

        controller.undoStack()->undo();
        QCOMPARE(activeLayerPixels(controller.document()), before);
        controller.undoStack()->redo();
        QCOMPARE(activeLayerPixels(controller.document()), expected);
    }

    void movesFrozenFillSelectionAsRenderedPixels()
    {
        Document document = Document::createDefault(QSize(96, 48));
        document.background = Qt::transparent;
        document.wobbleAmount = 0.0;
        Stroke sourceFill;
        sourceFill.mode = StrokeMode::Fill;
        sourceFill.color = QColor(120, 60, 220);
        sourceFill.points = {{QPointF(8.0, 8.0), 1.0}};
        sourceFill.fillMask = rectangularMask(
            document.size,
            QRect(8, 12, 24, 25));
        Stroke destinationPaint = makeStroke(
            StrokeMode::Paint,
            QColor(30, 180, 160),
            20.0,
            91,
            {QPointF(52.0, 24.0), QPointF(68.0, 24.0)});
        destinationPaint.brush.antialiasing = false;
        document.layers.first().strokes = {
            sourceFill,
            destinationPaint
        };

        const QImage selection = rectangularMask(
            document.size,
            QRect(8, 12, 24, 25));
        const QPoint delta(40, 0);
        const QImage before = activeLayerPixels(document);
        const QImage expected =
            rasterSelectionResult(before, selection, delta, true);
        QVERIFY(!before.isNull());
        QVERIFY(!expected.isNull());

        DocumentController controller;
        controller.loadDocument(document);
        QVERIFY(controller.moveStrokes(
            document.activeLayerId,
            {sourceFill.id},
            delta,
            selection));
        QCOMPARE(activeLayerPixels(controller.document()), expected);

        controller.undoStack()->undo();
        QCOMPARE(activeLayerPixels(controller.document()), before);
        controller.undoStack()->redo();
        QCOMPARE(activeLayerPixels(controller.document()), expected);
    }

    void movesOverlappingSelectionFromAnImmutableSnapshot()
    {
        Document document = Document::createDefault(QSize(80, 48));
        document.background = Qt::transparent;
        document.wobbleAmount = 0.0;
        Stroke paint = makeStroke(
            StrokeMode::Paint,
            QColor(35, 110, 225),
            20.0,
            101,
            {QPointF(10.0, 24.0), QPointF(44.0, 24.0)});
        Stroke erase = makeStroke(
            StrokeMode::Erase,
            Qt::black,
            8.0,
            102,
            {QPointF(20.0, 24.0), QPointF(25.0, 24.0)});
        paint.brush.antialiasing = false;
        erase.brush.antialiasing = false;
        document.layers.first().strokes = {paint, erase};

        const QImage selection = rectangularMask(
            document.size,
            QRect(2, 10, 46, 29));
        const QPoint delta(14, 0);
        const QImage before = activeLayerPixels(document);
        const QImage expected =
            rasterSelectionResult(before, selection, delta, true);
        QVERIFY(!before.isNull());
        QVERIFY(!expected.isNull());

        DocumentController controller;
        controller.loadDocument(document);
        QVERIFY(controller.moveStrokes(
            document.activeLayerId,
            {paint.id, erase.id},
            delta,
            selection));

        const QImage actual =
            activeLayerPixels(controller.document());
        QCOMPARE(actual, expected);
        QCOMPARE(actual.pixel(42, 24), expected.pixel(42, 24));
        QCOMPARE(actual.pixelColor(5, 24), QColor(Qt::transparent));

        controller.undoStack()->undo();
        QCOMPARE(activeLayerPixels(controller.document()), before);
        controller.undoStack()->redo();
        QCOMPARE(activeLayerPixels(controller.document()), expected);
    }

    void laterPaintAndEraseStayAboveCommittedSelectionPixels()
    {
        Document document = Document::createDefault(QSize(112, 64));
        document.background = Qt::transparent;
        document.animationFrames = 8;
        document.wobbleAmount = 5.0;
        Stroke sourcePaint = makeStroke(
            StrokeMode::Paint,
            QColor(40, 110, 225),
            20.0,
            111,
            {QPointF(12.0, 30.0), QPointF(34.0, 25.0)});
        Stroke sourceErase = makeStroke(
            StrokeMode::Erase,
            Qt::black,
            7.0,
            112,
            {QPointF(22.0, 28.0)});
        Stroke existingDestination = makeStroke(
            StrokeMode::Paint,
            QColor(225, 70, 55),
            22.0,
            113,
            {QPointF(64.0, 30.0), QPointF(88.0, 30.0)});
        Stroke laterPaint = makeStroke(
            StrokeMode::Paint,
            QColor(250, 205, 45),
            9.0,
            114,
            {QPointF(58.0, 22.0), QPointF(94.0, 38.0)});
        Stroke laterErase = makeStroke(
            StrokeMode::Erase,
            Qt::black,
            5.0,
            115,
            {QPointF(78.0, 20.0), QPointF(78.0, 42.0)});
        for (Stroke *stroke : {
                 &sourcePaint,
                 &sourceErase,
                 &existingDestination,
                 &laterPaint,
                 &laterErase}) {
            stroke->brush.antialiasing = false;
        }
        document.layers.first().strokes = {
            sourcePaint,
            sourceErase,
            existingDestination
        };

        const QImage selection = rectangularMask(
            document.size,
            QRect(2, 10, 42, 41));
        const QPoint delta(52, 0);
        QVector<QImage> beforeFrames;
        QVector<QImage> movedFrames;
        QVector<QImage> expectedFrames;
        beforeFrames.reserve(document.animationFrames);
        movedFrames.reserve(document.animationFrames);
        expectedFrames.reserve(document.animationFrames);
        for (int frame = 0; frame < document.animationFrames; ++frame) {
            const QImage before = activeLayerPixels(document, frame);
            const QImage moved =
                rasterSelectionResult(before, selection, delta, true);
            const QImage expected = renderAdditionalStrokes(
                moved,
                document,
                {laterPaint, laterErase},
                frame);
            QVERIFY(!before.isNull());
            QVERIFY(!moved.isNull());
            QVERIFY(!expected.isNull());
            beforeFrames.append(before);
            movedFrames.append(moved);
            expectedFrames.append(expected);
        }

        DocumentController controller;
        controller.loadDocument(document);
        QVERIFY(controller.moveStrokes(
            document.activeLayerId,
            {sourcePaint.id, sourceErase.id},
            delta,
            selection));
        controller.addStroke(document.activeLayerId, laterPaint);
        controller.addStroke(document.activeLayerId, laterErase);

        for (int frame = 0; frame < document.animationFrames; ++frame) {
            QCOMPARE(
                activeLayerPixels(controller.document(), frame),
                expectedFrames[frame]);
        }

        controller.undoStack()->undo();
        controller.undoStack()->undo();
        for (int frame = 0; frame < document.animationFrames; ++frame) {
            QCOMPARE(
                activeLayerPixels(controller.document(), frame),
                movedFrames[frame]);
        }
        controller.undoStack()->undo();
        for (int frame = 0; frame < document.animationFrames; ++frame) {
            QCOMPARE(
                activeLayerPixels(controller.document(), frame),
                beforeFrames[frame]);
        }
    }

    void appliesSequentialSelectionTransformsAsSeparateRasterOperations()
    {
        Document document = Document::createDefault(QSize(112, 56));
        document.background = Qt::transparent;
        document.animationFrames = 5;
        document.wobbleAmount = 3.0;
        Stroke blue = makeStroke(
            StrokeMode::Paint,
            QColor(35, 95, 220),
            15.0,
            121,
            {QPointF(10.0, 18.0), QPointF(30.0, 18.0)});
        Stroke green = makeStroke(
            StrokeMode::Paint,
            QColor(45, 190, 105),
            9.0,
            122,
            {QPointF(12.0, 34.0), QPointF(22.0, 27.0)});
        Stroke erase = makeStroke(
            StrokeMode::Erase,
            Qt::black,
            5.0,
            123,
            {QPointF(16.0, 18.0)});
        Stroke destination = makeStroke(
            StrokeMode::Paint,
            QColor(225, 65, 75),
            25.0,
            124,
            {QPointF(60.0, 28.0), QPointF(90.0, 28.0)});
        for (Stroke *stroke : {&blue, &green, &erase, &destination}) {
            stroke->brush.antialiasing = false;
        }
        document.layers.first().strokes = {
            blue,
            green,
            erase,
            destination
        };

        const QRect sourceRect(2, 8, 38, 39);
        const QImage firstSelection =
            rectangularMask(document.size, sourceRect);
        const QPoint delta(52, 0);
        QTransform moveTransform;
        moveTransform.translate(delta.x(), delta.y());
        const QImage secondSelection =
            transformedSelectionMask(firstSelection, moveTransform);
        QVERIFY(!secondSelection.isNull());
        const QPointF flipCenter =
            QRectF(sourceRect.translated(delta)).center();
        QTransform flipTransform;
        flipTransform.translate(flipCenter.x(), flipCenter.y());
        flipTransform.scale(-1.0, 1.0);
        flipTransform.translate(-flipCenter.x(), -flipCenter.y());

        QVector<QImage> beforeFrames;
        QVector<QImage> afterMoveFrames;
        QVector<QImage> afterFlipFrames;
        for (int frame = 0; frame < document.animationFrames; ++frame) {
            const QImage before = activeLayerPixels(document, frame);
            const QImage afterMove =
                rasterSelectionTransformResult(
                    before,
                    firstSelection,
                    moveTransform,
                    true);
            const QImage afterFlip =
                rasterSelectionTransformResult(
                    afterMove,
                    secondSelection,
                    flipTransform,
                    true);
            QVERIFY(!before.isNull());
            QVERIFY(!afterMove.isNull());
            QVERIFY(!afterFlip.isNull());
            beforeFrames.append(before);
            afterMoveFrames.append(afterMove);
            afterFlipFrames.append(afterFlip);
        }

        const QVector<QUuid> sourceIds = {
            blue.id,
            green.id,
            erase.id
        };
        DocumentController controller;
        controller.loadDocument(document);
        QVERIFY(controller.moveStrokes(
            document.activeLayerId,
            sourceIds,
            delta,
            firstSelection));
        QVERIFY(controller.flipStrokes(
            document.activeLayerId,
            sourceIds,
            flipCenter,
            true,
            secondSelection));
        for (int frame = 0; frame < document.animationFrames; ++frame) {
            QCOMPARE(
                activeLayerPixels(controller.document(), frame),
                afterFlipFrames[frame]);
        }

        controller.undoStack()->undo();
        for (int frame = 0; frame < document.animationFrames; ++frame) {
            QCOMPARE(
                activeLayerPixels(controller.document(), frame),
                afterMoveFrames[frame]);
        }
        controller.undoStack()->undo();
        for (int frame = 0; frame < document.animationFrames; ++frame) {
            QCOMPARE(
                activeLayerPixels(controller.document(), frame),
                beforeFrames[frame]);
        }
        controller.undoStack()->redo();
        controller.undoStack()->redo();
        for (int frame = 0; frame < document.animationFrames; ++frame) {
            QCOMPARE(
                activeLayerPixels(controller.document(), frame),
                afterFlipFrames[frame]);
        }
    }

    void replaysSelectionTransformForEveryConfiguredAnimationFrame()
    {
        Document document = Document::createDefault(QSize(88, 48));
        document.background = Qt::transparent;
        document.animationFrames =
            DocumentLimits::maximumAnimationFrames;
        document.wobbleAmount = 8.0;
        Stroke animated = makeStroke(
            StrokeMode::Paint,
            QColor(85, 55, 220),
            13.0,
            131,
            {
                QPointF(7.0, 13.0),
                QPointF(18.0, 35.0),
                QPointF(32.0, 17.0)
            });
        animated.brush.antialiasing = false;
        animated.brush.animatedJitter = true;
        document.layers.first().strokes = {animated};

        const QImage selection = rectangularMask(
            document.size,
            QRect(0, 4, 40, 40));
        const QPoint delta(42, 0);
        QVector<QImage> expectedFrames;
        expectedFrames.reserve(document.animationFrames);
        bool sawAnimation = false;
        QImage firstBefore;
        for (int frame = 0; frame < document.animationFrames; ++frame) {
            const QImage before = activeLayerPixels(document, frame);
            if (frame == 0) {
                firstBefore = before;
            } else if (before != firstBefore) {
                sawAnimation = true;
            }
            expectedFrames.append(
                rasterSelectionResult(before, selection, delta, true));
            QVERIFY(!expectedFrames.last().isNull());
        }
        QVERIFY(sawAnimation);

        DocumentController controller;
        controller.loadDocument(document);
        QVERIFY(controller.moveStrokes(
            document.activeLayerId,
            {animated.id},
            delta,
            selection));
        QCOMPARE(
            controller.document().animationFrames,
            DocumentLimits::maximumAnimationFrames);
        for (int frame = 0; frame < document.animationFrames; ++frame) {
            QCOMPARE(
                activeLayerPixels(controller.document(), frame),
                expectedFrames[frame]);
        }
    }

    void nonUniformImageResizeScalesCommittedLayerPixelsExactly()
    {
        Document document = Document::createDefault(QSize(73, 47));
        document.background = Qt::transparent;
        document.animationFrames = 6;
        document.wobbleAmount = 4.0;
        Stroke paint = makeStroke(
            StrokeMode::Paint,
            QColor(30, 135, 225),
            11.0,
            141,
            {
                QPointF(6.0, 8.0),
                QPointF(31.0, 38.0),
                QPointF(61.0, 13.0)
            });
        Stroke erase = makeStroke(
            StrokeMode::Erase,
            Qt::black,
            5.0,
            142,
            {QPointF(28.0, 29.0), QPointF(42.0, 23.0)});
        Stroke fill;
        fill.mode = StrokeMode::Fill;
        fill.color = QColor(235, 180, 40);
        fill.points = {{QPointF(4.0, 4.0), 1.0}};
        fill.fillMask = rectangularMask(
            document.size,
            QRect(2, 2, 12, 9));
        paint.brush.antialiasing = false;
        erase.brush.antialiasing = false;
        document.layers.first().strokes = {paint, erase, fill};

        const QSize targetSize(119, 61);
        QVector<QImage> beforeFrames;
        QVector<QImage> expectedFrames;
        for (int frame = 0; frame < document.animationFrames; ++frame) {
            const QImage before = activeLayerPixels(document, frame);
            const QImage expected =
                resizedRasterResult(before, targetSize, true);
            QVERIFY(!before.isNull());
            QVERIFY(!expected.isNull());
            beforeFrames.append(before);
            expectedFrames.append(expected);
        }

        DocumentController controller;
        controller.loadDocument(document);
        QVERIFY(controller.resizeImage(targetSize));
        QCOMPARE(controller.document().size, targetSize);
        for (int frame = 0; frame < document.animationFrames; ++frame) {
            QCOMPARE(
                activeLayerPixels(controller.document(), frame),
                expectedFrames[frame]);
        }

        controller.undoStack()->undo();
        QCOMPARE(controller.document().size, document.size);
        for (int frame = 0; frame < document.animationFrames; ++frame) {
            QCOMPARE(
                activeLayerPixels(controller.document(), frame),
                beforeFrames[frame]);
        }
        controller.undoStack()->redo();
        QCOMPARE(controller.document().size, targetSize);
        for (int frame = 0; frame < document.animationFrames; ++frame) {
            QCOMPARE(
                activeLayerPixels(controller.document(), frame),
                expectedFrames[frame]);
        }
    }

    void canvasCropThenExpandKeepsPreexistingSelectionEditClipped()
    {
        Document document = Document::createDefault(QSize(96, 48));
        document.background = Qt::transparent;
        document.animationFrames = 4;
        document.wobbleAmount = 3.0;
        Stroke paint = makeStroke(
            StrokeMode::Paint,
            QColor(50, 120, 230),
            17.0,
            151,
            {QPointF(8.0, 24.0), QPointF(30.0, 24.0)});
        Stroke erase = makeStroke(
            StrokeMode::Erase,
            Qt::black,
            5.0,
            152,
            {QPointF(17.0, 24.0)});
        paint.brush.antialiasing = false;
        erase.brush.antialiasing = false;
        document.layers.first().strokes = {paint, erase};

        const QImage selection = rectangularMask(
            document.size,
            QRect(0, 10, 40, 29));
        const QPoint moveDelta(48, 0);
        const QSize croppedSize(68, 48);
        const QPoint cropOffset(0, 0);
        const QSize expandedSize(96, 48);
        const QPoint expandOffset(0, 0);
        QVector<QImage> movedFrames;
        QVector<QImage> croppedFrames;
        QVector<QImage> expandedFrames;
        for (int frame = 0; frame < document.animationFrames; ++frame) {
            const QImage before = activeLayerPixels(document, frame);
            const QImage moved =
                rasterSelectionResult(
                    before,
                    selection,
                    moveDelta,
                    true);
            const QImage cropped =
                reframedRasterResult(
                    moved,
                    croppedSize,
                    cropOffset);
            const QImage expanded =
                reframedRasterResult(
                    cropped,
                    expandedSize,
                    expandOffset);
            QVERIFY(!moved.isNull());
            QVERIFY(!cropped.isNull());
            QVERIFY(!expanded.isNull());
            movedFrames.append(moved);
            croppedFrames.append(cropped);
            expandedFrames.append(expanded);
        }

        DocumentController controller;
        controller.loadDocument(document);
        QVERIFY(controller.moveStrokes(
            document.activeLayerId,
            {paint.id, erase.id},
            moveDelta,
            selection));
        for (int frame = 0; frame < document.animationFrames; ++frame) {
            QCOMPARE(
                activeLayerPixels(controller.document(), frame),
                movedFrames[frame]);
        }

        QVERIFY(controller.resizeCanvas(croppedSize, cropOffset));
        for (int frame = 0; frame < document.animationFrames; ++frame) {
            QCOMPARE(
                activeLayerPixels(controller.document(), frame),
                croppedFrames[frame]);
        }

        QVERIFY(controller.resizeCanvas(expandedSize, expandOffset));
        for (int frame = 0; frame < document.animationFrames; ++frame) {
            const QImage actual =
                activeLayerPixels(controller.document(), frame);
            QCOMPARE(actual, expandedFrames[frame]);
            QCOMPARE(actual.pixelColor(80, 24), QColor(Qt::transparent));
        }

        controller.undoStack()->undo();
        for (int frame = 0; frame < document.animationFrames; ++frame) {
            QCOMPARE(
                activeLayerPixels(controller.document(), frame),
                croppedFrames[frame]);
        }
        controller.undoStack()->undo();
        for (int frame = 0; frame < document.animationFrames; ++frame) {
            QCOMPARE(
                activeLayerPixels(controller.document(), frame),
                movedFrames[frame]);
        }
    }

    void full4kSelectionUsesPackedStorageAndBoundedFrameRendering()
    {
        constexpr int edge = DocumentLimits::maximumCanvasEdge;
        const QSize canvasSize(edge, edge);
        Document document = Document::createDefault(canvasSize);
        document.background = Qt::transparent;
        document.animationFrames =
            DocumentLimits::maximumAnimationFrames;
        document.wobbleAmount = 5.0;
        Stroke paint = makeStroke(
            StrokeMode::Paint,
            QColor(45, 105, 225),
            5.0,
            161,
            {
                QPointF(16.0, edge / 2.0),
                QPointF(edge - 16.0, edge / 2.0)
            });
        paint.brush.antialiasing = false;
        document.layers.first().strokes = {paint};

        QImage selection(canvasSize, QImage::Format_Grayscale8);
        QVERIFY(!selection.isNull());
        quint32 random = 0x6d2b79f5U;
        for (int y = 0; y < edge; ++y) {
            uchar *line = selection.scanLine(y);
            for (int x = 0; x < edge; ++x) {
                random ^= random << 13U;
                random ^= random >> 17U;
                random ^= random << 5U;
                line[x] = (random & 1U) != 0U ? 255 : 0;
            }
        }
        selection.scanLine(0)[0] = 255;
        selection.scanLine(edge - 1)[edge - 1] = 255;

        DocumentController controller;
        controller.loadDocument(document);
        QVERIFY(controller.moveStrokes(
            document.activeLayerId,
            {paint.id},
            QPointF(0.0, 1.0),
            selection));

        const Layer &layer = controller.document().layers.first();
        QVERIFY(!layer.strokes.isEmpty());
        const Stroke &operation = layer.strokes.last();
        QCOMPARE(operation.mode, StrokeMode::PixelSelection);
        QVERIFY(operation.pixelSelectionOp.has_value());
        const PixelSelectionOp &pixelOperation =
            *operation.pixelSelectionOp;
        QCOMPARE(pixelOperation.canvasSize, canvasSize);
        QCOMPARE(pixelOperation.sourceBounds, QRect(QPoint(), canvasSize));
        const qsizetype expectedStride =
            (static_cast<qsizetype>(edge) + 7) / 8;
        const qsizetype expectedPackedBytes =
            expectedStride * edge;
        QCOMPARE(expectedPackedBytes, qsizetype(2 * 1024 * 1024));
        QCOMPARE(
            pixelOperation.packedMask.size(),
            expectedPackedBytes);
        QCOMPARE(
            packedSelectionBytes(controller.document()),
            quint64(expectedPackedBytes));
        QVERIFY(
            packedSelectionBytes(controller.document())
            <= DocumentLimits::maximumDistinctClipMaskBytes);

        selection = {};
        const quint64 expectedFrameBytes =
            static_cast<quint64>(edge)
            * static_cast<quint64>(edge)
            * sizeof(QRgb);
        QCOMPARE(
            expectedFrameBytes,
            quint64(64) * 1024ULL * 1024ULL);
        QImage rendered =
            activeLayerPixels(controller.document(), 0);
        QVERIFY(!rendered.isNull());
        QCOMPARE(rendered.size(), canvasSize);
        QCOMPARE(
            static_cast<quint64>(rendered.sizeInBytes()),
            expectedFrameBytes);
        rendered = {};
        rendered = activeLayerPixels(
            controller.document(),
            DocumentLimits::maximumAnimationFrames - 1);
        QVERIFY(!rendered.isNull());
        QCOMPARE(rendered.size(), canvasSize);
        QCOMPARE(
            static_cast<quint64>(rendered.sizeInBytes()),
            expectedFrameBytes);
    }

    void usesTabletPressureForWidth()
    {
        auto renderPressure = [](qreal pressure) {
            Document document = Document::createDefault(QSize(80, 64));
            document.wobbleAmount = 0.0;
            Stroke stroke;
            stroke.color = Qt::black;
            stroke.width = 20.0;
            stroke.points = {
                {QPointF(10.0, 32.0), pressure},
                {QPointF(70.0, 32.0), pressure}
            };
            document.layers.first().strokes.append(stroke);
            return RenderEngine::render(document, 0);
        };

        const QImage light = renderPressure(0.1);
        const QImage heavy = renderPressure(1.0);
        int lightPixels = 0;
        int heavyPixels = 0;
        for (int y = 0; y < light.height(); ++y) {
            for (int x = 0; x < light.width(); ++x) {
                if (light.pixelColor(x, y) != QColor(Qt::white)) {
                    ++lightPixels;
                }
                if (heavy.pixelColor(x, y) != QColor(Qt::white)) {
                    ++heavyPixels;
                }
            }
        }
        QVERIFY(heavyPixels > lightPixels * 2);
    }

    void rendersEveryBuiltInBrushDeterministically()
    {
        QSet<QString> ids;
        for (const BrushPreset &preset : BrushPresetCatalog::builtIns()) {
            QVERIFY2(!ids.contains(preset.id), qPrintable(preset.id));
            ids.insert(preset.id);
            QVERIFY(isValidBrushSettings(preset.settings));

            Document document = Document::createDefault(QSize(128, 96));
            document.wobbleAmount = 0.0;
            Stroke stroke;
            stroke.seed = 0x123456789abcdef0ULL;
            stroke.color = QColor(20, 40, 80);
            stroke.width = std::min(64.0, preset.defaultSize);
            stroke.brush = preset.settings;
            stroke.points = {
                {QPointF(24.0, 48.0), 0.45},
                {QPointF(104.0, 48.0), 1.0}
            };
            document.layers.first().strokes.append(stroke);

            const QImage first = RenderEngine::render(document, 3);
            const QImage second = RenderEngine::render(document, 3);
            QVERIFY2(!first.isNull(), qPrintable(preset.id));
            QVERIFY2(first == second, qPrintable(preset.id));
            QVERIFY2(
                std::any_of(
                    first.constBits(),
                    first.constBits() + first.sizeInBytes(),
                    [](uchar value) { return value != 255; }),
                qPrintable(preset.id));
        }
        QCOMPARE(ids.size(), BrushPresetCatalog::builtIns().size());
    }

    void rendersSoftAirbrushWithPartialAlpha()
    {
        Document document = Document::createDefault(QSize(80, 80));
        document.background = Qt::transparent;
        document.wobbleAmount = 0.0;
        Stroke stroke;
        stroke.seed = 77;
        stroke.color = Qt::black;
        stroke.width = 48.0;
        stroke.brush =
            BrushPresetCatalog::find(QStringLiteral("soft-airbrush"))->settings;
        stroke.points = {
            {QPointF(40.0, 40.0), 1.0}
        };
        document.layers.first().strokes.append(stroke);

        const QImage image = RenderEngine::render(document, 0);
        QVERIFY(!image.isNull());
        const int centerAlpha = image.pixelColor(40, 40).alpha();
        const int middleAlpha = image.pixelColor(52, 40).alpha();
        const int edgeAlpha = image.pixelColor(64, 40).alpha();
        QVERIFY(centerAlpha > middleAlpha);
        QVERIFY(middleAlpha > edgeAlpha);
        QVERIFY(centerAlpha > 0 && centerAlpha < 255);
    }

    void onlyAnimatedSprayChangesWithoutWobble()
    {
        auto renderPreset = [](const QString &presetId, int frame) {
            Document document = Document::createDefault(QSize(96, 72));
            document.wobbleAmount = 0.0;
            Stroke stroke;
            stroke.seed = 91;
            stroke.color = Qt::black;
            stroke.width = 44.0;
            stroke.brush = BrushPresetCatalog::find(presetId)->settings;
            stroke.points = {
                {QPointF(18.0, 36.0), 1.0},
                {QPointF(78.0, 36.0), 1.0}
            };
            document.layers.first().strokes.append(stroke);
            return RenderEngine::render(document, frame);
        };

        QCOMPARE(
            renderPreset(QStringLiteral("pixel-spray"), 0),
            renderPreset(QStringLiteral("pixel-spray"), 1));
        QVERIFY(
            renderPreset(QStringLiteral("wobble-spray"), 0)
            != renderPreset(QStringLiteral("wobble-spray"), 1));
    }

    void handlesDotsAndDuplicatePoints()
    {
        Document document = Document::createDefault(QSize(64, 64));
        document.wobbleAmount = 5.0;

        const Stroke dot = makeStroke(
            StrokeMode::Paint,
            Qt::black,
            8.0,
            100,
            {QPointF(16.0, 16.0)});
        const Stroke duplicates = makeStroke(
            StrokeMode::Paint,
            QColor(200, 20, 40),
            8.0,
            200,
            {
                QPointF(40.0, 40.0),
                QPointF(40.0, 40.0),
                QPointF(40.0, 40.0)
            });
        document.layers.first().strokes = {dot, duplicates};

        const QPainterPath dotPath =
            RenderEngine::strokePath(dot, 3, document.animationFrames, 5.0);
        const QPainterPath duplicatePath =
            RenderEngine::strokePath(duplicates, 3, document.animationFrames, 5.0);
        QVERIFY(dotPath.elementCount() > 0);
        QVERIFY(duplicatePath.elementCount() > 0);
        const QPainterPath::Element dotElement = dotPath.elementAt(0);
        const QPainterPath::Element duplicateElement = duplicatePath.elementAt(0);
        QVERIFY(std::isfinite(dotElement.x));
        QVERIFY(std::isfinite(dotElement.y));
        QVERIFY(std::isfinite(duplicateElement.x));
        QVERIFY(std::isfinite(duplicateElement.y));

        const QImage image = RenderEngine::render(document, 3);
        QVERIFY(!image.isNull());
        QCOMPARE(image.size(), document.size);
        bool containsPaint = false;
        for (int y = 0; y < image.height() && !containsPaint; ++y) {
            for (int x = 0; x < image.width(); ++x) {
                if (image.pixelColor(x, y) != document.background) {
                    containsPaint = true;
                    break;
                }
            }
        }
        QVERIFY(containsPaint);
    }
};

int runRenderEngineTests(int argc, char **argv)
{
    RenderEngineTests tests;
    return QTest::qExec(&tests, argc, argv);
}

}

#include "RenderEngineTests.moc"
