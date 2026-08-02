#include "brush/BrushPreset.hpp"
#include "document/DocumentController.hpp"
#include "document/DocumentLimits.hpp"
#include "document/SelectionOperation.hpp"
#include "document/SelectionVisibility.hpp"
#include "io/DocumentSerializer.hpp"
#include "render/ImageAffineTransformer.hpp"
#include "render/ImageResampler.hpp"
#include "render/IncrementalStrokeRenderer.hpp"
#include "render/PreviewRenderPolicy.hpp"
#include "render/RenderEngine.hpp"

#include <QElapsedTimer>
#include <QPainter>
#include <QRandomGenerator>
#include <QSet>
#include <QTransform>
#include <QtTest>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <functional>
#include <limits>
#include <numbers>

namespace wobble
{

namespace
{

Stroke makeStroke(StrokeMode mode,
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
    for (const QPointF &position : positions)
    {
        stroke.points.append({position, 1.0});
    }
    return stroke;
}

Document animatedDocument()
{
    Document document = Document::createDefault(QSize(96, 72));
    document.animationFrames = 12;
    document.wobbleAmount = 6.0;
    document.layers.first().strokes.append(makeStroke(StrokeMode::Paint,
        QColor(25, 40, 70),
        9.0,
        0x123456789abcdef0ULL,
        {QPointF(8.0, 52.0),
            QPointF(24.0, 18.0),
            QPointF(48.0, 46.0),
            QPointF(72.0, 14.0),
            QPointF(88.0, 48.0)}));
    return document;
}

QImage rectangularMask(const QSize &size, const QRect &rect)
{
    QImage mask(size, QImage::Format_Grayscale8);
    mask.fill(0);
    const QRect clipped = rect.intersected(mask.rect());
    for (int y = clipped.top(); y <= clipped.bottom(); ++y)
    {
        std::fill(mask.scanLine(y) + clipped.left(),
            mask.scanLine(y) + clipped.right() + 1,
            255);
    }
    return mask;
}

QPainter::CompositionMode referenceCompositionMode(LayerBlendMode mode)
{
    switch (mode)
    {
    case LayerBlendMode::Multiply:
        return QPainter::CompositionMode_Multiply;
    case LayerBlendMode::Screen:
        return QPainter::CompositionMode_Screen;
    case LayerBlendMode::Overlay:
        return QPainter::CompositionMode_Overlay;
    case LayerBlendMode::Normal:
        return QPainter::CompositionMode_SourceOver;
    }
    return QPainter::CompositionMode_SourceOver;
}

QImage referenceHierarchyComposition(const Document &document,
    const QHash<QUuid, QImage> &paintLayers,
    const QSize &outputSize)
{
    std::function<QImage(const QUuid &, bool)> composeStack;
    composeStack = [&](const QUuid &parentGroupId, bool root)
    {
        QImage result(outputSize, QImage::Format_ARGB32_Premultiplied);
        if (result.isNull())
        {
            return QImage();
        }
        result.fill(root ? document.background : QColor(Qt::transparent));

        QImage clippingBase;
        qreal clippingBaseOpacity = 0.0;
        for (const Layer &layer : document.layers)
        {
            if (layer.parentGroupId != parentGroupId)
            {
                continue;
            }
            if (!layer.visible || layer.opacity <= 0.0)
            {
                if (!layer.clipToLayerBelow)
                {
                    clippingBase =
                        QImage(outputSize, QImage::Format_ARGB32_Premultiplied);
                    if (!clippingBase.isNull())
                    {
                        clippingBase.fill(Qt::transparent);
                    }
                    clippingBaseOpacity = 0.0;
                }
                continue;
            }

            QImage layerImage = layer.kind == LayerKind::Group
                                    ? composeStack(layer.id, false)
                                    : paintLayers.value(layer.id);
            if (layerImage.isNull())
            {
                return QImage();
            }
            if (layer.clipToLayerBelow)
            {
                if (clippingBase.isNull() || clippingBaseOpacity <= 0.0)
                {
                    continue;
                }
                QPainter clipper(&layerImage);
                clipper.setCompositionMode(
                    QPainter::CompositionMode_DestinationIn);
                clipper.setOpacity(clippingBaseOpacity);
                clipper.drawImage(QPoint(), clippingBase);
                clipper.end();
            }
            else
            {
                clippingBase = layerImage;
                clippingBaseOpacity = std::clamp(layer.opacity, 0.0, 1.0);
            }

            QPainter painter(&result);
            painter.setCompositionMode(
                referenceCompositionMode(layer.blendMode));
            painter.setOpacity(std::clamp(layer.opacity, 0.0, 1.0));
            painter.drawImage(QPoint(), layerImage);
            painter.end();
        }
        return result;
    };
    return composeStack({}, true);
}

QImage patternedSurface(
    const QSize &size, const QColor &first, const QColor &second)
{
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    if (image.isNull())
    {
        return {};
    }
    for (int y = 0; y < size.height(); ++y)
    {
        for (int x = 0; x < size.width(); ++x)
        {
            image.setPixelColor(x, y, (x + y) % 2 == 0 ? first : second);
        }
    }
    return image;
}

Document adversarialClippedHierarchy(const QSize &size, int groupCount)
{
    Document document = Document::createDefault(size);
    document.background = QColor(24, 32, 48);
    document.layers.clear();
    document.activeLayerId = {};

    QUuid parentGroupId;
    for (int depth = 0; depth <= groupCount; ++depth)
    {
        Layer base;
        base.name = QStringLiteral("Base %1").arg(depth);
        base.parentGroupId = parentGroupId;
        base.initialCanvasSize = size;
        document.layers.append(base);
        if (document.activeLayerId.isNull())
        {
            document.activeLayerId = base.id;
        }
        if (depth == groupCount)
        {
            Layer clipped;
            clipped.name = QStringLiteral("Clipped");
            clipped.parentGroupId = parentGroupId;
            clipped.clipToLayerBelow = true;
            clipped.initialCanvasSize = size;
            document.layers.append(clipped);
            break;
        }

        Layer group;
        group.name = QStringLiteral("Group %1").arg(depth);
        group.kind = LayerKind::Group;
        group.parentGroupId = parentGroupId;
        group.initialCanvasSize = size;
        document.layers.append(group);
        parentGroupId = group.id;
    }
    return document;
}

QImage activeLayerPixels(const Document &document, int frameIndex = 0)
{
    QImage layerImage;
    if (document.layers.isEmpty()
        || !RenderEngine::renderStrokesOnLayer(layerImage,
            document,
            document.layers.first().strokes,
            frameIndex,
            document.size))
    {
        return {};
    }
    return layerImage;
}

QImage rasterSelectionResult(const QImage &before,
    const QImage &selection,
    const QPoint &delta,
    bool cutSource)
{
    if (before.isNull() || selection.size() != before.size()
        || selection.format() != QImage::Format_Grayscale8)
    {
        return {};
    }

    QImage payload(before.size(), QImage::Format_ARGB32_Premultiplied);
    payload.fill(Qt::transparent);
    QImage result = before;
    for (int y = 0; y < before.height(); ++y)
    {
        const QRgb *source =
            reinterpret_cast<const QRgb *>(before.constScanLine(y));
        QRgb *payloadLine = reinterpret_cast<QRgb *>(payload.scanLine(y));
        QRgb *resultLine = reinterpret_cast<QRgb *>(result.scanLine(y));
        const uchar *selectionLine = selection.constScanLine(y);
        for (int x = 0; x < before.width(); ++x)
        {
            if (selectionLine[x] < 128)
            {
                continue;
            }
            payloadLine[x] = source[x];
            if (cutSource)
            {
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

QImage rasterSelectionTransformResult(const QImage &before,
    const QImage &selection,
    const QTransform &transform,
    bool cutSource,
    bool smooth = false)
{
    if (before.isNull() || selection.size() != before.size()
        || selection.format() != QImage::Format_Grayscale8
        || !transform.isInvertible())
    {
        return {};
    }

    QImage payload(before.size(), QImage::Format_ARGB32_Premultiplied);
    payload.fill(Qt::transparent);
    QImage result = before;
    for (int y = 0; y < before.height(); ++y)
    {
        const QRgb *source =
            reinterpret_cast<const QRgb *>(before.constScanLine(y));
        QRgb *payloadLine = reinterpret_cast<QRgb *>(payload.scanLine(y));
        QRgb *resultLine = reinterpret_cast<QRgb *>(result.scanLine(y));
        const uchar *selectionLine = selection.constScanLine(y);
        for (int x = 0; x < before.width(); ++x)
        {
            if (selectionLine[x] < 128)
            {
                continue;
            }
            payloadLine[x] = source[x];
            if (cutSource)
            {
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
    const QImage &selection, const QTransform &transform)
{
    if (selection.isNull() || selection.format() != QImage::Format_Grayscale8
        || !transform.isInvertible())
    {
        return {};
    }
    QImage transformed(selection.size(), QImage::Format_Grayscale8);
    if (transformed.isNull())
    {
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

QImage renderAdditionalStrokes(QImage base,
    const Document &document,
    const QVector<Stroke> &strokes,
    int frameIndex)
{
    if (!RenderEngine::renderStrokesOnLayer(
            base, document, strokes, frameIndex, base.size()))
    {
        return {};
    }
    return base;
}

QImage resizedRasterResult(
    const QImage &source, const QSize &targetSize, bool smooth)
{
    return ImageResampler::resample(source,
        targetSize,
        smooth ? SamplingMode::Smooth : SamplingMode::Nearest);
}

QImage qtResizedRasterResult(
    const QImage &source, const QSize &targetSize, bool smooth)
{
    QImage target(targetSize, QImage::Format_ARGB32_Premultiplied);
    if (source.isNull() || !targetSize.isValid() || target.isNull())
    {
        return {};
    }
    target.fill(Qt::transparent);
    QPainter painter(&target);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, smooth);
    painter.drawImage(QRectF(QPointF(), QSizeF(targetSize)),
        source,
        QRectF(QPointF(), QSizeF(source.size())));
    painter.end();
    return target;
}

QImage qtAffineComposite(QImage target,
    const QRect &targetBounds,
    const QImage &source,
    const QRect &sourceBounds,
    const QTransform &transform,
    SamplingMode sampling)
{
    if (target.isNull() || target.size() != targetBounds.size()
        || source.isNull() || source.size() != sourceBounds.size())
    {
        return {};
    }
    QPainter painter(&target);
    painter.setRenderHint(
        QPainter::SmoothPixmapTransform, sampling == SamplingMode::Smooth);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    QTransform targetTransform;
    targetTransform.translate(-targetBounds.left(), -targetBounds.top());
    painter.setWorldTransform(transform * targetTransform);
    painter.drawImage(sourceBounds.topLeft(), source);
    painter.end();
    return target;
}

QImage reframedRasterResult(
    const QImage &source, const QSize &targetSize, const QPoint &contentOffset)
{
    if (source.isNull() || !targetSize.isValid())
    {
        return {};
    }
    QImage target(targetSize, QImage::Format_ARGB32_Premultiplied);
    if (target.isNull())
    {
        return {};
    }
    target.fill(Qt::transparent);
    QPainter painter(&target);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.drawImage(contentOffset, source);
    painter.end();
    return target;
}

QImage clearedSelectionResult(const QImage &before, const QImage &selection)
{
    if (before.isNull() || selection.size() != before.size()
        || selection.format() != QImage::Format_Grayscale8)
    {
        return {};
    }
    QImage result = before;
    for (int y = 0; y < result.height(); ++y)
    {
        QRgb *resultLine = reinterpret_cast<QRgb *>(result.scanLine(y));
        const uchar *selectionLine = selection.constScanLine(y);
        for (int x = 0; x < result.width(); ++x)
        {
            if (selectionLine[x] >= 128)
            {
                resultLine[x] = 0;
            }
        }
    }
    return result;
}

QImage expandedStrokeCoverage(
    const RenderEngine::StrokeCoverageRegion &coverage, const QSize &size)
{
    if (!coverage.valid || !size.isValid())
    {
        return {};
    }
    QImage expanded(size, QImage::Format_ARGB32_Premultiplied);
    if (expanded.isNull())
    {
        return {};
    }
    expanded.fill(Qt::transparent);
    if (!coverage.image.isNull())
    {
        QPainter painter(&expanded);
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.drawImage(coverage.bounds.topLeft(), coverage.image);
    }
    return expanded;
}

struct ImageDifference
{
    quint64 channelDifference = 0;
    quint64 comparedChannels = 0;
    qsizetype visiblyDifferentPixels = 0;
    int maximumChannelDifference = 0;
};

ImageDifference imageDifference(
    const QImage &left, const QImage &right, int visibleThreshold = 24)
{
    ImageDifference difference;
    if (left.isNull() || right.isNull() || left.size() != right.size())
    {
        difference.channelDifference = std::numeric_limits<quint64>::max();
        return difference;
    }
    difference.comparedChannels = static_cast<quint64>(left.width())
                                  * static_cast<quint64>(left.height()) * 4;
    for (int y = 0; y < left.height(); ++y)
    {
        for (int x = 0; x < left.width(); ++x)
        {
            const QColor a = left.pixelColor(x, y);
            const QColor b = right.pixelColor(x, y);
            const int red = std::abs(a.red() - b.red());
            const int green = std::abs(a.green() - b.green());
            const int blue = std::abs(a.blue() - b.blue());
            const int alpha = std::abs(a.alpha() - b.alpha());
            difference.maximumChannelDifference =
                std::max(difference.maximumChannelDifference,
                    std::max({red, green, blue, alpha}));
            difference.channelDifference +=
                static_cast<quint64>(red + green + blue + alpha);
            if (std::max({red, green, blue, alpha}) > visibleThreshold)
            {
                ++difference.visiblyDifferentPixels;
            }
        }
    }
    return difference;
}

ImageDifference storedImageDifference(
    const QImage &left, const QImage &right, int visibleThreshold = 24)
{
    ImageDifference difference;
    if (left.isNull() || right.isNull() || left.size() != right.size())
    {
        difference.channelDifference = std::numeric_limits<quint64>::max();
        return difference;
    }
    difference.comparedChannels = static_cast<quint64>(left.width())
                                  * static_cast<quint64>(left.height()) * 4;
    for (int y = 0; y < left.height(); ++y)
    {
        const auto *leftLine =
            reinterpret_cast<const QRgb *>(left.constScanLine(y));
        const auto *rightLine =
            reinterpret_cast<const QRgb *>(right.constScanLine(y));
        for (int x = 0; x < left.width(); ++x)
        {
            const int red = std::abs(qRed(leftLine[x]) - qRed(rightLine[x]));
            const int green =
                std::abs(qGreen(leftLine[x]) - qGreen(rightLine[x]));
            const int blue = std::abs(qBlue(leftLine[x]) - qBlue(rightLine[x]));
            const int alpha =
                std::abs(qAlpha(leftLine[x]) - qAlpha(rightLine[x]));
            const int maximum = std::max({red, green, blue, alpha});
            difference.channelDifference +=
                static_cast<quint64>(red + green + blue + alpha);
            difference.maximumChannelDifference =
                std::max(difference.maximumChannelDifference, maximum);
            if (maximum > visibleThreshold)
            {
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
            rendered.pixelColor(rendered.width() - 1, rendered.height() - 1),
            document.background);
    }

    void rendersScaledPreview()
    {
        const Document document = animatedDocument();
        const QSize previewSize(48, 36);
        RenderEngine::ScaledRenderStats stats;
        const QImage preview = RenderEngine::renderScaled(document,
            5,
            previewSize,
            RenderEngine::ScaledRenderMode::DisplayPreview,
            &stats);

        QVERIFY(!preview.isNull());
        QCOMPARE(preview.size(), previewSize);
        QVERIFY(stats.usedDisplayScaleReplay);
        QVERIFY(!stats.usedNativeExactFallback);
        bool containsPaint = false;
        for (int y = 0; y < preview.height() && !containsPaint; ++y)
        {
            for (int x = 0; x < preview.width(); ++x)
            {
                if (preview.pixelColor(x, y) != document.background)
                {
                    containsPaint = true;
                    break;
                }
            }
        }
        QVERIFY(containsPaint);
        QVERIFY(RenderEngine::renderScaled(document, 0, {}).isNull());

        const QImage exact = RenderEngine::renderScaled(document,
            5,
            previewSize,
            RenderEngine::ScaledRenderMode::NativeExact,
            &stats);
        QVERIFY(!exact.isNull());
        QVERIFY(!stats.usedDisplayScaleReplay);
        QVERIFY(stats.usedNativeExactFallback);

        const QImage nativeSized = RenderEngine::renderScaled(document,
            5,
            document.size,
            RenderEngine::ScaledRenderMode::DisplayPreview,
            &stats);
        QCOMPARE(nativeSized, RenderEngine::render(document, 5));
        QVERIFY(!stats.usedDisplayScaleReplay);
        QVERIFY(stats.usedNativeExactFallback);
    }

    void roundsPreviewCacheCostUpToWholeKibibytes()
    {
        QCOMPARE(PreviewRenderPolicy::cacheCostKiB(-1), 1);
        QCOMPARE(PreviewRenderPolicy::cacheCostKiB(0), 1);
        QCOMPARE(PreviewRenderPolicy::cacheCostKiB(1), 1);
        QCOMPARE(PreviewRenderPolicy::cacheCostKiB(1024), 1);
        QCOMPARE(PreviewRenderPolicy::cacheCostKiB(1025), 2);
        QCOMPARE(PreviewRenderPolicy::cacheCostKiB(2048), 2);
        QCOMPARE(PreviewRenderPolicy::cacheCostKiB(2049), 3);
        QCOMPARE(PreviewRenderPolicy::cacheCostKiB(
                     std::numeric_limits<qsizetype>::max()),
            std::numeric_limits<int>::max());
    }

    void boundsAnimatedPreviewByFrameCacheBudget()
    {
        const QSize documentSize(4096, 4096);
        const QSize staticPreview =
            PreviewRenderPolicy::renderSize(documentSize, 16.0);
        const QSize animatedPreview =
            PreviewRenderPolicy::renderSize(documentSize, 16.0, 30);

        QCOMPARE(staticPreview, documentSize);
        QVERIFY(animatedPreview.width() < documentSize.width());
        QCOMPARE(animatedPreview.width(), animatedPreview.height());
        const quint64 retainedBytes =
            static_cast<quint64>(animatedPreview.width())
            * animatedPreview.height() * sizeof(quint32) * 30;
        const quint64 cacheBytes =
            static_cast<quint64>(PreviewRenderPolicy::maximumCacheKiB) * 1024;
        QVERIFY(retainedBytes <= cacheBytes);
        const int retainedCost =
            PreviewRenderPolicy::cacheCostKiB(
                static_cast<qsizetype>(animatedPreview.width())
                * animatedPreview.height() * sizeof(quint32))
            * 30;
        QVERIFY(retainedCost <= PreviewRenderPolicy::maximumCacheKiB);
        QCOMPARE(PreviewRenderPolicy::renderSize(documentSize, 16.0, 0),
            staticPreview);
    }

    void replaysIntegralNearestSelectionAtDisplayScale()
    {
        Document document = Document::createDefault(QSize(128, 96));
        document.background = Qt::transparent;
        document.wobbleAmount = 0.0;
        Layer &layer = document.layers.first();
        layer.initialCanvasSize = document.size;

        Stroke block = makeStroke(StrokeMode::Paint,
            QColor(210, 40, 70),
            32.0,
            901,
            {QPointF(24.0, 24.0)});
        block.brush.tipShape = BrushTipShape::Square;
        block.brush.antialiasing = false;
        block.brush.sizeDynamics = 0.0;
        block.brush.wobbleScale = 0.0;
        layer.strokes.append(block);

        const QImage selection =
            rectangularMask(document.size, QRect(8, 8, 32, 32));
        QTransform shift;
        shift.translate(64.0, 32.0);
        const std::optional<PixelSelectionOp> selectionOperation =
            makePixelSelectionOp(selection, shift, true, true);
        QVERIFY(selectionOperation.has_value());
        QCOMPARE(selectionOperation->sampling, SamplingMode::Nearest);
        Stroke operation;
        operation.mode = StrokeMode::PixelSelection;
        operation.pixelSelectionOp = *selectionOperation;
        layer.strokes.append(operation);

        Stroke overlay = makeStroke(StrokeMode::Paint,
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
        const QImage preview = RenderEngine::renderScaled(document,
            0,
            outputSize,
            RenderEngine::ScaledRenderMode::DisplayPreview,
            &stats);
        const QImage exact = RenderEngine::renderScaled(document,
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

        Stroke block = makeStroke(StrokeMode::Paint,
            QColor(195, 65, 215),
            32.0,
            903,
            {QPointF(32.0, 32.0)});
        block.brush.tipShape = BrushTipShape::Square;
        block.brush.antialiasing = false;
        block.brush.sizeDynamics = 0.0;
        block.brush.wobbleScale = 0.0;
        layer.strokes.append(block);

        const QImage selection =
            rectangularMask(document.size, QRect(16, 16, 32, 32));
        // x' = 96 - y, y' = x: an integral quarter turn whose display
        // transform is anisotropic at the requested 1/4 x 1/8 scale.
        const QTransform quarterTurn(
            0.0, 1.0, 0.0, -1.0, 0.0, 0.0, 96.0, 0.0, 1.0);
        const std::optional<PixelSelectionOp> pixelOperation =
            makePixelSelectionOp(selection, quarterTurn, true, true);
        QVERIFY(pixelOperation.has_value());
        QCOMPARE(pixelOperation->sampling, SamplingMode::Nearest);
        Stroke operation;
        operation.mode = StrokeMode::PixelSelection;
        operation.pixelSelectionOp = *pixelOperation;
        layer.strokes.append(operation);

        const QSize outputSize(32, 12);
        const QImage preview =
            RenderEngine::renderScaled(document, 0, outputSize);
        const QImage exact = RenderEngine::renderScaled(document,
            0,
            outputSize,
            RenderEngine::ScaledRenderMode::NativeExact);
        QVERIFY(!preview.isNull());
        QCOMPARE(preview, exact);
    }

    void matchesNativeExactForRandomIntegralSelectionReplays()
    {
        quint32 random = 0x8f3a91d7U;
        const auto next = [&random]()
        {
            random ^= random << 13U;
            random ^= random >> 17U;
            random ^= random << 5U;
            return random;
        };

        for (int caseIndex = 0; caseIndex < 24; ++caseIndex)
        {
            Document document = Document::createDefault(QSize(128, 96));
            document.background = Qt::transparent;
            document.wobbleAmount = 0.0;
            Layer &layer = document.layers.first();
            layer.initialCanvasSize = document.size;

            const int blockEdge = 16 + static_cast<int>(next() % 3) * 8;
            const int sourceX = 8 + static_cast<int>(next() % 9) * 4;
            const int sourceY = 8 + static_cast<int>(next() % 7) * 4;
            Stroke block = makeStroke(StrokeMode::Paint,
                QColor(40 + static_cast<int>(next() % 180),
                    40 + static_cast<int>(next() % 180),
                    40 + static_cast<int>(next() % 180)),
                blockEdge,
                next(),
                {QPointF(
                    sourceX + blockEdge * 0.5, sourceY + blockEdge * 0.5)});
            block.brush.tipShape = BrushTipShape::Square;
            block.brush.antialiasing = false;
            block.brush.sizeDynamics = 0.0;
            block.brush.wobbleScale = 0.0;
            layer.strokes.append(block);

            const QImage selection = rectangularMask(
                document.size, QRect(sourceX, sourceY, blockEdge, blockEdge));
            const int deltaX = 48 + static_cast<int>(next() % 5) * 4;
            const int deltaY = (static_cast<int>(next() % 7) - 3) * 4;
            QTransform transform;
            transform.translate(deltaX, deltaY);
            const bool clearSource = caseIndex % 3 != 1;
            const bool drawDestination = caseIndex % 3 != 2;
            const std::optional<PixelSelectionOp> pixelOperation =
                makePixelSelectionOp(
                    selection, transform, clearSource, drawDestination);
            QVERIFY(pixelOperation.has_value());
            QCOMPARE(pixelOperation->sampling, SamplingMode::Nearest);
            Stroke operation;
            operation.mode = StrokeMode::PixelSelection;
            operation.pixelSelectionOp = *pixelOperation;
            layer.strokes.append(operation);

            const QSize outputSize(32, 24);
            const QImage preview =
                RenderEngine::renderScaled(document, caseIndex, outputSize);
            const QImage exact = RenderEngine::renderScaled(document,
                caseIndex,
                outputSize,
                RenderEngine::ScaledRenderMode::NativeExact);
            QVERIFY2(preview == exact,
                qPrintable(QStringLiteral("integral replay case %1 diverged")
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

        Stroke first = makeStroke(StrokeMode::Paint,
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
        crop.reframeOp = ReframeOp{ReframeMode::Canvas,
            SamplingMode::Nearest,
            QSize(128, 96),
            QSize(96, 72),
            QPoint(-16, -8)};
        layer.strokes.append(crop);

        Stroke second = makeStroke(StrokeMode::Paint,
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
        expand.reframeOp = ReframeOp{ReframeMode::Canvas,
            SamplingMode::Nearest,
            QSize(96, 72),
            QSize(128, 96),
            QPoint(12, 8)};
        layer.strokes.append(expand);

        const QSize outputSize(32, 24);
        RenderEngine::ScaledRenderStats stats;
        const QImage preview = RenderEngine::renderScaled(document,
            0,
            outputSize,
            RenderEngine::ScaledRenderMode::DisplayPreview,
            &stats);
        const QImage exact = RenderEngine::renderScaled(document,
            0,
            outputSize,
            RenderEngine::ScaledRenderMode::NativeExact);
        QVERIFY(!preview.isNull());
        QCOMPARE(preview, exact);
        QVERIFY(stats.usedDisplayScaleReplay);
        QCOMPARE(stats.largestIntermediateImageSize, outputSize);
        QVERIFY(stats.largestIntermediateImageBytes
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

        Stroke block = makeStroke(StrokeMode::Paint,
            QColor(45, 115, 225),
            56.0,
            921,
            {QPointF(52.0, 48.0)});
        block.brush.tipShape = BrushTipShape::Square;
        block.brush.antialiasing = false;
        block.brush.sizeDynamics = 0.0;
        block.brush.wobbleScale = 0.0;
        layer.strokes.append(block);

        Stroke circle = makeStroke(StrokeMode::Paint,
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
        resize.reframeOp = ReframeOp{ReframeMode::Image,
            SamplingMode::Smooth,
            QSize(160, 120),
            document.size,
            QPoint()};
        layer.strokes.append(resize);

        const QSize outputSize(24, 18);
        RenderEngine::ScaledRenderStats stats;
        const QImage preview = RenderEngine::renderScaled(document,
            0,
            outputSize,
            RenderEngine::ScaledRenderMode::DisplayPreview,
            &stats);
        const QImage exact = RenderEngine::renderScaled(document,
            0,
            outputSize,
            RenderEngine::ScaledRenderMode::NativeExact);
        QVERIFY(!preview.isNull());
        QVERIFY(!exact.isNull());
        const ImageDifference difference = imageDifference(preview, exact);
        QVERIFY(difference.comparedChannels > 0);
        const qreal meanChannelDifference =
            static_cast<qreal>(difference.channelDifference)
            / difference.comparedChannels;
        QVERIFY2(meanChannelDifference < 8.0,
            qPrintable(QStringLiteral("mean channel difference: %1")
                    .arg(meanChannelDifference)));
        QVERIFY(difference.visiblyDifferentPixels
                < outputSize.width() * outputSize.height() / 5);
        QVERIFY(stats.usedDisplayScaleReplay);
        QCOMPARE(stats.largestIntermediateImageSize, QSize(40, 30));
        QVERIFY(stats.largestIntermediateImageBytes
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

        Stroke animated = makeStroke(StrokeMode::Paint,
            QColor(205, 55, 120),
            18.0,
            931,
            {QPointF(20.0, 72.0),
                QPointF(54.0, 28.0),
                QPointF(92.0, 76.0),
                QPointF(124.0, 32.0)});
        animated.brush.antialiasing = true;
        animated.brush.animatedJitter = true;
        layer.strokes.append(animated);

        const QImage selection =
            rectangularMask(document.size, QRect(8, 12, 132, 84));
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

        Stroke overlay = makeStroke(StrokeMode::Paint,
            QColor(35, 190, 115),
            10.0,
            932,
            {QPointF(24.0, 18.0), QPointF(132.0, 102.0)});
        overlay.brush.antialiasing = true;
        overlay.brush.wobbleScale = 0.0;
        layer.strokes.append(overlay);

        const QSize outputSize(40, 30);
        QVector<QImage> previews;
        for (const int frame : {0, 1, 5})
        {
            const QImage preview =
                RenderEngine::renderScaled(document, frame, outputSize);
            const QImage exact = RenderEngine::renderScaled(document,
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
            QVERIFY2(meanChannelDifference < 12.0,
                qPrintable(
                    QStringLiteral("frame %1 mean channel difference: %2")
                        .arg(frame)
                        .arg(meanChannelDifference)));
            QVERIFY(difference.visiblyDifferentPixels
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
        Stroke bottomStroke = makeStroke(StrokeMode::Paint,
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
        Stroke middleStroke = makeStroke(StrokeMode::Paint,
            QColor(220, 70, 50),
            32.0,
            942,
            {QPointF(36.0, 52.0)});
        middleStroke.brush.tipShape = BrushTipShape::Square;
        middleStroke.brush.sizeDynamics = 0.0;
        middle.strokes.append(middleStroke);
        const QImage selection =
            rectangularMask(document.size, QRect(20, 36, 32, 32));
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
        top.strokes.append(makeStroke(StrokeMode::Paint,
            QColor(30, 180, 95),
            12.0,
            943,
            {QPointF(12.0, 84.0), QPointF(116.0, 12.0)}));
        document.layers.append(top);

        const QSize outputSize(32, 24);
        RenderEngine::ScaledRenderStats stats;
        const RenderEngine::LayerSplitFrame split =
            RenderEngine::renderLayerSplit(document,
                0,
                outputSize,
                middleId,
                RenderEngine::ScaledRenderMode::DisplayPreview,
                &stats);
        QVERIFY(split.valid);
        const QImage composed =
            RenderEngine::composeLayerSplit(split, split.layerBase);
        const QImage full = RenderEngine::renderScaled(document, 0, outputSize);
        QCOMPARE(composed, full);
        QVERIFY(stats.usedDisplayScaleReplay);
        QVERIFY(stats.packedSelectionSamples > 0);
        QVERIFY(stats.largestIntermediateImageBytes
                <= static_cast<quint64>(outputSize.width())
                       * outputSize.height() * sizeof(QRgb));
        const quint64 frameBytes = static_cast<quint64>(outputSize.width())
                                   * outputSize.height() * sizeof(QRgb);
        QVERIFY(stats.maximumEstimatedWorkingSetBytes <= frameBytes * 4);
    }

    void replaysPendingSelectionOnCachedLayerFramebuffer()
    {
        Document document = Document::createDefault(QSize(128, 96));
        document.background = QColor(248, 245, 238);
        document.wobbleAmount = 0.0;
        Layer &target = document.layers.first();
        const QUuid targetId = target.id;
        target.initialCanvasSize = document.size;
        Stroke block = makeStroke(StrokeMode::Paint,
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
        top.strokes.append(makeStroke(StrokeMode::Paint,
            QColor(220, 60, 70),
            10.0,
            945,
            {QPointF(8.0, 88.0), QPointF(120.0, 8.0)}));
        document.layers.append(top);

        const QImage selection =
            rectangularMask(document.size, QRect(16, 32, 32, 32));
        QTransform shift;
        shift.translate(64.0, 8.0);
        const std::optional<PixelSelectionOp> operation =
            makePixelSelectionOp(selection, shift, true, true);
        QVERIFY(operation.has_value());

        const QSize outputSize(32, 24);
        const RenderEngine::LayerSplitFrame split =
            RenderEngine::renderLayerSplit(document, 0, outputSize, targetId);
        QVERIFY(split.valid);
        const QImage cachedBase = split.layerBase;
        QImage previewLayer = split.layerBase;
        RenderEngine::ScaledRenderStats previewStats;
        QVERIFY(RenderEngine::replayPixelSelectionOnLayer(previewLayer,
            *operation,
            RenderEngine::ScaledRenderMode::DisplayPreview,
            &previewStats));
        QCOMPARE(split.layerBase, cachedBase);
        QVERIFY(previewStats.usedDisplayScaleReplay);
        QVERIFY(!previewStats.usedNativeExactFallback);
        QCOMPARE(previewStats.primitiveStrokesRendered, quint64(0));
        QCOMPARE(previewStats.pixelSelectionOperationsReplayed, quint64(1));
        QVERIFY(previewStats.packedSelectionSamples > 0);
        const RenderEngine::PixelSelectionPreviewRegion previewRegion =
            RenderEngine::replayPixelSelectionOnLayerRegion(
                split.layerBase, *operation);
        QVERIFY(previewRegion.valid);
        QVERIFY(!previewRegion.bounds.isEmpty());
        QImage regionalPreviewLayer = split.layerBase;
        QPainter regionalPreviewPainter(&regionalPreviewLayer);
        regionalPreviewPainter.setCompositionMode(
            QPainter::CompositionMode_Source);
        regionalPreviewPainter.drawImage(
            previewRegion.bounds.topLeft(), previewRegion.image);
        regionalPreviewPainter.end();
        QCOMPARE(regionalPreviewLayer, previewLayer);
        QVERIFY(
            previewRegion.image.sizeInBytes() < split.layerBase.sizeInBytes());

        Document expectedPreview = document;
        Stroke previewOperation;
        previewOperation.mode = StrokeMode::PixelSelection;
        previewOperation.pixelSelectionOp = *operation;
        expectedPreview.layers.first().strokes.append(previewOperation);
        QCOMPARE(RenderEngine::composeLayerSplit(split, previewLayer),
            RenderEngine::renderScaled(expectedPreview, 0, outputSize));

        const RenderEngine::LayerSplitFrame nativeSplit =
            RenderEngine::renderLayerSplit(
                document, 0, document.size, targetId);
        QVERIFY(nativeSplit.valid);
        QImage nativeLayer = nativeSplit.layerBase;
        RenderEngine::ScaledRenderStats nativeStats;
        QVERIFY(RenderEngine::replayPixelSelectionOnLayer(nativeLayer,
            *operation,
            RenderEngine::ScaledRenderMode::NativeExact,
            &nativeStats));
        QVERIFY(!nativeStats.usedDisplayScaleReplay);
        QVERIFY(nativeStats.usedNativeExactFallback);
        QCOMPARE(nativeStats.primitiveStrokesRendered, quint64(0));
        QCOMPARE(nativeStats.pixelSelectionOperationsReplayed, quint64(1));
        const RenderEngine::PixelSelectionPreviewRegion nativeRegion =
            RenderEngine::replayPixelSelectionOnLayerRegion(
                nativeSplit.layerBase, *operation);
        QVERIFY(nativeRegion.valid);
        QVERIFY(!nativeRegion.bounds.isEmpty());
        QImage regionalNativeLayer = nativeSplit.layerBase;
        QPainter regionalNativePainter(&regionalNativeLayer);
        regionalNativePainter.setCompositionMode(
            QPainter::CompositionMode_Source);
        regionalNativePainter.drawImage(
            nativeRegion.bounds.topLeft(), nativeRegion.image);
        regionalNativePainter.end();
        QCOMPARE(regionalNativeLayer, nativeLayer);
        QVERIFY(nativeRegion.image.sizeInBytes()
                < nativeSplit.layerBase.sizeInBytes());
        QCOMPARE(RenderEngine::composeLayerSplit(nativeSplit, nativeLayer),
            RenderEngine::render(expectedPreview, 0));
    }

    void replaysPendingSelectionWhenOnlyOnePreviewAxisShrinks()
    {
        const auto verify = [](const QSize &canvasSize,
                                const QSize &previewSize,
                                const QRect &selectionBounds,
                                const QPointF &translation)
        {
            Document document = Document::createDefault(canvasSize);
            document.background = Qt::transparent;
            document.wobbleAmount = 0.0;
            Layer &layer = document.layers.first();
            layer.initialCanvasSize = canvasSize;

            Stroke source = makeStroke(StrokeMode::Paint,
                QColor(45, 105, 225),
                1.0,
                946,
                {QPointF(selectionBounds.center())});
            source.brush.tipShape = BrushTipShape::Square;
            source.brush.antialiasing = false;
            source.brush.sizeDynamics = 0.0;
            source.brush.wobbleScale = 0.0;
            layer.strokes.append(source);

            const QImage selection =
                rectangularMask(canvasSize, selectionBounds);
            QTransform transform;
            transform.translate(translation.x(), translation.y());
            const std::optional<PixelSelectionOp> operation =
                makePixelSelectionOp(selection, transform, true, true);
            QVERIFY(operation.has_value());

            RenderEngine::ScaledRenderStats splitStats;
            const RenderEngine::LayerSplitFrame split =
                RenderEngine::renderLayerSplit(document,
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
            QVERIFY(RenderEngine::replayPixelSelectionOnLayer(previewLayer,
                *operation,
                RenderEngine::ScaledRenderMode::DisplayPreview,
                &replayStats));
            QVERIFY(replayStats.usedDisplayScaleReplay);
            QVERIFY(!replayStats.usedNativeExactFallback);
            QCOMPARE(replayStats.primitiveStrokesRendered, quint64(0));
            QCOMPARE(replayStats.pixelSelectionOperationsReplayed, quint64(1));

            Document expected = document;
            Stroke selectionOperation;
            selectionOperation.mode = StrokeMode::PixelSelection;
            selectionOperation.pixelSelectionOp = *operation;
            expected.layers.first().strokes.append(selectionOperation);
            QCOMPARE(RenderEngine::composeLayerSplit(split, previewLayer),
                RenderEngine::renderScaled(expected,
                    0,
                    previewSize,
                    RenderEngine::ScaledRenderMode::DisplayPreview));
        };

        verify(QSize(4096, 1),
            QSize(410, 1),
            QRect(512, 0, 1024, 1),
            QPointF(1024.0, 0.0));
        verify(QSize(1, 4096),
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
        for (int index = 0; index < DocumentLimits::maximumStrokesPerLayer;
            ++index)
        {
            Stroke dot = makeStroke(StrokeMode::Paint,
                QColor(35, 95, 225, 24),
                1.0,
                static_cast<quint64>(index + 1),
                {QPointF(32.0 + index % 64, 32.0 + (index / 64) % 64)});
            dot.brush.antialiasing = false;
            dot.brush.sizeDynamics = 0.0;
            dot.brush.wobbleScale = 0.0;
            layer.strokes.append(std::move(dot));
        }

        const QSize outputSize(32, 32);
        RenderEngine::ScaledRenderStats splitStats;
        const RenderEngine::LayerSplitFrame split =
            RenderEngine::renderLayerSplit(document,
                0,
                outputSize,
                layer.id,
                RenderEngine::ScaledRenderMode::DisplayPreview,
                &splitStats);
        QVERIFY(split.valid);
        QCOMPARE(splitStats.primitiveStrokesRendered,
            static_cast<quint64>(DocumentLimits::maximumStrokesPerLayer));

        const QImage selection =
            rectangularMask(document.size, QRect(24, 24, 80, 80));
        QTransform shift;
        shift.translate(8.0, 0.0);
        const std::optional<PixelSelectionOp> operation =
            makePixelSelectionOp(selection, shift, true, true);
        QVERIFY(operation.has_value());
        const QImage cachedBase = split.layerBase;
        for (int preview = 0; preview < 4; ++preview)
        {
            QImage layerPreview = split.layerBase;
            RenderEngine::ScaledRenderStats replayStats;
            QVERIFY(RenderEngine::replayPixelSelectionOnLayer(layerPreview,
                *operation,
                RenderEngine::ScaledRenderMode::DisplayPreview,
                &replayStats));
            QCOMPARE(replayStats.primitiveStrokesRendered, quint64(0));
            QCOMPARE(replayStats.pixelSelectionOperationsReplayed, quint64(1));
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

        Stroke block = makeStroke(StrokeMode::Paint,
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
        const qsizetype packedStride = (static_cast<qsizetype>(edge) + 7) / 8;
        pixelOperation.packedMask =
            QByteArray(packedStride * edge, static_cast<char>(0xff));
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
        const QImage preview = RenderEngine::renderScaled(document,
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
            static_cast<quint64>(outputSize.width()) * outputSize.height()
            * sizeof(QRgb);
        const quint64 nativeFrameBytes =
            static_cast<quint64>(edge) * edge * sizeof(QRgb);
        QCOMPARE(stats.largestIntermediateImageBytes, displayFrameBytes);
        QVERIFY(stats.maximumEstimatedWorkingSetBytes < nativeFrameBytes);
        QCOMPARE(stats.packedSelectionSamples,
            static_cast<quint64>(outputSize.width()) * outputSize.height());
        QCOMPARE(pixelOperation.packedMask.size(), qsizetype(2 * 1024 * 1024));
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
        for (int frame = 1; frame < document.animationFrames; ++frame)
        {
            QCOMPARE(RenderEngine::render(document, frame), first);
        }
    }

    void staysStillWhenBrushWobbleScaleIsZero()
    {
        Document document = animatedDocument();
        document.layers.first().strokes.first().brush.wobbleScale = 0.0;

        const QImage first = RenderEngine::render(document, 0);
        QVERIFY(!first.isNull());
        for (int frame = 1; frame < document.animationFrames; ++frame)
        {
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

    void rendersLayerBlendModes_data()
    {
        QTest::addColumn<int>("blendMode");
        QTest::addColumn<int>("compositionMode");

        QTest::newRow("normal")
            << static_cast<int>(LayerBlendMode::Normal)
            << static_cast<int>(QPainter::CompositionMode_SourceOver);
        QTest::newRow("multiply")
            << static_cast<int>(LayerBlendMode::Multiply)
            << static_cast<int>(QPainter::CompositionMode_Multiply);
        QTest::newRow("screen")
            << static_cast<int>(LayerBlendMode::Screen)
            << static_cast<int>(QPainter::CompositionMode_Screen);
        QTest::newRow("overlay")
            << static_cast<int>(LayerBlendMode::Overlay)
            << static_cast<int>(QPainter::CompositionMode_Overlay);
    }

    void rendersLayerBlendModes()
    {
        QFETCH(int, blendMode);
        QFETCH(int, compositionMode);

        const QColor background(60, 130, 210);
        const QColor source(210, 70, 125);
        Document document = Document::createDefault(QSize(16, 16));
        document.background = background;
        document.wobbleAmount = 0.0;
        Layer &layer = document.layers.first();
        layer.opacity = 0.75;
        layer.blendMode = static_cast<LayerBlendMode>(blendMode);
        Stroke stroke =
            makeStroke(StrokeMode::Paint, source, 16.0, 1, {QPointF(8.0, 8.0)});
        stroke.brush.tipShape = BrushTipShape::Square;
        stroke.brush.sizeDynamics = 0.0;
        stroke.brush.wobbleScale = 0.0;
        stroke.brush.antialiasing = false;
        layer.strokes.append(stroke);

        const QImage rendered = RenderEngine::render(document, 0);
        QVERIFY(!rendered.isNull());

        QImage expected(1, 1, QImage::Format_ARGB32_Premultiplied);
        expected.fill(background);
        QPainter painter(&expected);
        painter.setCompositionMode(
            static_cast<QPainter::CompositionMode>(compositionMode));
        painter.setOpacity(layer.opacity);
        painter.fillRect(expected.rect(), source);
        painter.end();

        const QColor actual = rendered.pixelColor(8, 8);
        const QColor reference = expected.pixelColor(0, 0);
        QVERIFY(std::abs(actual.red() - reference.red()) <= 1);
        QVERIFY(std::abs(actual.green() - reference.green()) <= 1);
        QVERIFY(std::abs(actual.blue() - reference.blue()) <= 1);
        QCOMPARE(actual.alpha(), reference.alpha());
    }

    void preservesHierarchyOpacityBlendAndClippingPixels()
    {
        const QSize size(5, 4);
        Document document = Document::createDefault(size);
        document.background = QColor(35, 55, 80, 230);
        document.layers.clear();
        document.activeLayerId = {};

        Layer rootBase;
        rootBase.name = QStringLiteral("Root base");
        rootBase.opacity = 0.8;
        rootBase.blendMode = LayerBlendMode::Multiply;
        rootBase.initialCanvasSize = size;

        Layer group;
        group.name = QStringLiteral("Group");
        group.kind = LayerKind::Group;
        group.opacity = 0.7;
        group.blendMode = LayerBlendMode::Screen;
        group.initialCanvasSize = size;

        Layer childBase;
        childBase.name = QStringLiteral("Child base");
        childBase.parentGroupId = group.id;
        childBase.opacity = 0.65;
        childBase.blendMode = LayerBlendMode::Overlay;
        childBase.initialCanvasSize = size;

        Layer childClipped;
        childClipped.name = QStringLiteral("Child clipped");
        childClipped.parentGroupId = group.id;
        childClipped.clipToLayerBelow = true;
        childClipped.opacity = 0.55;
        childClipped.blendMode = LayerBlendMode::Multiply;
        childClipped.initialCanvasSize = size;

        Layer top;
        top.name = QStringLiteral("Top");
        top.clipToLayerBelow = true;
        top.opacity = 0.35;
        top.blendMode = LayerBlendMode::Screen;
        top.initialCanvasSize = size;

        document.layers = {rootBase, group, childBase, childClipped, top};
        document.activeLayerId = rootBase.id;
        QVERIFY(!DocumentSerializer::toJson(document).isEmpty());

        RenderEngine::LayerRasterFrame frame;
        frame.outputSize = size;
        frame.paintLayers.insert(rootBase.id,
            patternedSurface(
                size, QColor(220, 45, 60, 210), QColor(45, 190, 100, 120)));
        frame.paintLayers.insert(childBase.id,
            patternedSurface(
                size, QColor(40, 95, 225, 190), QColor(230, 170, 40, 90)));
        frame.paintLayers.insert(childClipped.id,
            patternedSurface(
                size, QColor(220, 220, 245, 230), QColor(65, 25, 180, 160)));
        frame.paintLayers.insert(top.id,
            patternedSurface(
                size, QColor(20, 200, 210, 150), QColor(240, 80, 160, 70)));
        frame.valid = true;

        const QImage expected = referenceHierarchyComposition(
            document, frame.paintLayers, frame.outputSize);
        const QImage actual =
            RenderEngine::composeLayerRasterFrame(document, frame, {}, {});
        QVERIFY(!expected.isNull());
        QCOMPARE(actual, expected);
    }

    void preservesHiddenGroupSkipAndClippingResetPixels()
    {
        const QSize size(4, 3);
        Document document = Document::createDefault(size);
        document.background = QColor(30, 45, 65);
        document.layers.clear();

        Layer base;
        base.name = QStringLiteral("Base");
        base.initialCanvasSize = size;

        Layer hiddenGroup;
        hiddenGroup.name = QStringLiteral("Hidden group");
        hiddenGroup.kind = LayerKind::Group;
        hiddenGroup.visible = false;
        hiddenGroup.initialCanvasSize = size;

        Layer hiddenChild;
        hiddenChild.name = QStringLiteral("Hidden child");
        hiddenChild.parentGroupId = hiddenGroup.id;
        hiddenChild.initialCanvasSize = size;

        Layer clipped;
        clipped.name = QStringLiteral("Clipped after hidden group");
        clipped.clipToLayerBelow = true;
        clipped.initialCanvasSize = size;

        document.layers = {base, hiddenGroup, hiddenChild, clipped};
        document.activeLayerId = base.id;
        QVERIFY(!DocumentSerializer::toJson(document).isEmpty());

        RenderEngine::LayerRasterFrame frame;
        frame.outputSize = size;
        frame.paintLayers.insert(base.id,
            patternedSurface(
                size, QColor(210, 50, 65, 220), QColor(45, 175, 95, 130)));
        frame.paintLayers.insert(hiddenChild.id,
            patternedSurface(
                size, QColor(30, 80, 220, 240), QColor(230, 180, 45, 180)));
        frame.paintLayers.insert(clipped.id,
            patternedSurface(
                size, QColor(245, 245, 245, 255), QColor(20, 20, 20, 255)));
        frame.valid = true;

        const QImage expected = referenceHierarchyComposition(
            document, frame.paintLayers, frame.outputSize);
        const QImage actual =
            RenderEngine::composeLayerRasterFrame(document, frame, {}, {});
        QCOMPARE(actual, expected);
        const LayerCompositionMemoryEstimate estimate =
            RenderEngine::estimateHierarchyMemory(document, size);
        QVERIFY(estimate.valid);
        QCOMPARE(estimate.peakSurfaceCount, 2);
    }

    void measuresAdversarialHierarchyPeakSurfaces()
    {
        const Document document = adversarialClippedHierarchy(QSize(2, 2), 8);
        QVERIFY(!DocumentSerializer::toJson(document).isEmpty());
        const LayerCompositionMemoryEstimate estimate =
            RenderEngine::estimateHierarchyMemory(document, QSize(1, 1));
        QVERIFY(estimate.valid);
        QCOMPARE(estimate.peakSurfaceCount, 11);
        QCOMPARE(estimate.bytesPerSurface, 4ULL);
        QCOMPARE(estimate.peakBytes, 44ULL);

        RenderEngine::ScaledRenderStats stats;
        const QImage rendered = RenderEngine::renderScaled(document,
            0,
            QSize(1, 1),
            RenderEngine::ScaledRenderMode::DisplayPreview,
            &stats);
        QVERIFY(!rendered.isNull());
        QCOMPARE(stats.hierarchyPlannedPeakSurfaceCount, 11);
        QCOMPARE(stats.hierarchyPeakSurfaceCount, 11);
        QCOMPARE(stats.hierarchyPeakSurfaceBytes, 44ULL);
        QVERIFY(stats.hierarchySurfaceReuses >= 8);
    }

    void handlesLegacyOverDepthHierarchyIteratively()
    {
        const Document document = adversarialClippedHierarchy(QSize(2, 2), 64);
        QVERIFY(!DocumentSerializer::toJson(document).isEmpty());
        const LayerCompositionMemoryEstimate estimate =
            RenderEngine::estimateHierarchyMemory(document, QSize(1, 1));
        QVERIFY(estimate.valid);
        QCOMPARE(estimate.peakSurfaceCount, 67);

        RenderEngine::ScaledRenderStats stats;
        const QImage rendered = RenderEngine::renderScaled(document,
            0,
            QSize(1, 1),
            RenderEngine::ScaledRenderMode::DisplayPreview,
            &stats);
        QVERIFY(!rendered.isNull());
        QCOMPARE(stats.hierarchyPlannedPeakSurfaceCount, 67);
        QCOMPARE(stats.hierarchyPeakSurfaceCount, 67);
    }

    void reusesCompositionSurfacesAcrossSiblingGroups()
    {
        Document document = Document::createDefault(QSize(2, 2));
        document.layers.clear();
        document.activeLayerId = {};
        for (int index = 0; index < 2; ++index)
        {
            Layer group;
            group.name = QStringLiteral("Group %1").arg(index);
            group.kind = LayerKind::Group;
            group.initialCanvasSize = document.size;
            Layer child;
            child.name = QStringLiteral("Child %1").arg(index);
            child.parentGroupId = group.id;
            child.initialCanvasSize = document.size;
            document.layers.append(group);
            document.layers.append(child);
            if (document.activeLayerId.isNull())
            {
                document.activeLayerId = child.id;
            }
        }
        QVERIFY(!DocumentSerializer::toJson(document).isEmpty());

        RenderEngine::ScaledRenderStats stats;
        const QImage rendered = RenderEngine::renderScaled(document,
            0,
            QSize(1, 1),
            RenderEngine::ScaledRenderMode::DisplayPreview,
            &stats);
        QVERIFY(!rendered.isNull());
        QVERIFY(stats.hierarchySurfaceReuses > 0);
        QCOMPARE(stats.hierarchyPeakSurfaceCount,
            stats.hierarchyPlannedPeakSurfaceCount);
    }

    void includesHierarchyTransientSurfacesInFourKPreviewBudget()
    {
        const QSize fourK(4096, 4096);
        const Document document = adversarialClippedHierarchy(fourK, 8);
        QVERIFY(!DocumentSerializer::toJson(document).isEmpty());
        const LayerCompositionMemoryEstimate estimate =
            RenderEngine::estimateHierarchyMemory(document, fourK);
        QVERIFY(estimate.valid);
        QCOMPARE(estimate.peakSurfaceCount, 11);
        QCOMPARE(estimate.bytesPerSurface, 64ULL * 1024ULL * 1024ULL);
        QVERIFY(estimate.peakBytes
                > static_cast<quint64>(PreviewRenderPolicy::maximumCacheKiB)
                      * 1024ULL);

        const QSize preview = PreviewRenderPolicy::renderSize(
            fourK, 16.0, 1, estimate.peakSurfaceCount);
        QVERIFY(preview.isValid());
        QVERIFY(preview.width() < fourK.width());
        QCOMPARE(preview.width(), preview.height());
        const quint64 concurrentSurfaceCount = static_cast<quint64>(
            estimate.peakSurfaceCount
            + LayerCompositionPlan::paintOperationScratchSurfaceCount);
        const quint64 previewBytes = static_cast<quint64>(preview.width())
                                     * static_cast<quint64>(preview.height())
                                     * sizeof(quint32) * concurrentSurfaceCount;
        QVERIFY(previewBytes
                <= static_cast<quint64>(PreviewRenderPolicy::maximumCacheKiB)
                       * 1024ULL);
    }

    void rejectsOverflowingHierarchyMemoryEstimate()
    {
        const Document document = Document::createDefault(QSize(1, 1));
        const int maximum = std::numeric_limits<int>::max();
        const LayerCompositionMemoryEstimate estimate =
            RenderEngine::estimateHierarchyMemory(
                document, QSize(maximum, maximum));
        QVERIFY(!estimate.valid);
    }

    void rejectsClippedGroupCompositionPlan()
    {
        Document document = Document::createDefault(QSize(16, 16));
        Layer group;
        group.name = QStringLiteral("Invalid clipped group");
        group.kind = LayerKind::Group;
        group.clipToLayerBelow = true;
        group.initialCanvasSize = document.size;
        document.layers.append(group);
        QVERIFY(!RenderEngine::estimateHierarchyMemory(document, document.size)
                .valid);
        QVERIFY(RenderEngine::render(document, 0).isNull());
    }

    void clipsLayerToBaseAlphaWithinGroup()
    {
        Document document = Document::createDefault(QSize(32, 32));
        document.background = Qt::white;
        document.wobbleAmount = 0.0;
        Layer &base = document.layers.first();
        Stroke baseStroke = makeStroke(StrokeMode::Paint,
            QColor(220, 40, 50),
            8.0,
            1,
            {QPointF(16.0, 16.0)});
        baseStroke.brush.tipShape = BrushTipShape::Square;
        baseStroke.brush.sizeDynamics = 0.0;
        baseStroke.brush.wobbleScale = 0.0;
        baseStroke.brush.antialiasing = false;
        base.strokes.append(baseStroke);

        Layer clipped;
        clipped.name = QStringLiteral("Clipped");
        clipped.initialCanvasSize = document.size;
        clipped.clipToLayerBelow = true;
        Stroke clippedStroke = makeStroke(StrokeMode::Paint,
            QColor(30, 80, 220),
            24.0,
            2,
            {QPointF(16.0, 16.0)});
        clippedStroke.brush.tipShape = BrushTipShape::Square;
        clippedStroke.brush.sizeDynamics = 0.0;
        clippedStroke.brush.wobbleScale = 0.0;
        clippedStroke.brush.antialiasing = false;
        clipped.strokes.append(clippedStroke);
        document.layers.append(clipped);

        const QImage rendered = RenderEngine::render(document, 0);
        QVERIFY(!rendered.isNull());
        QCOMPARE(rendered.pixelColor(16, 16), QColor(30, 80, 220));
        QCOMPARE(rendered.pixelColor(7, 16), QColor(Qt::white));

        document.layers.last().clipToLayerBelow = false;
        const QImage unclipped = RenderEngine::render(document, 0);
        QCOMPARE(unclipped.pixelColor(7, 16), QColor(30, 80, 220));
    }

    void isolatesLayerGroupComposition()
    {
        Document document = Document::createDefault(QSize(24, 24));
        document.background = Qt::white;
        document.wobbleAmount = 0.0;
        Layer &child = document.layers.first();
        Stroke stroke = makeStroke(StrokeMode::Paint,
            QColor(200, 20, 40),
            12.0,
            3,
            {QPointF(12.0, 12.0)});
        stroke.brush.tipShape = BrushTipShape::Square;
        stroke.brush.sizeDynamics = 0.0;
        stroke.brush.wobbleScale = 0.0;
        stroke.brush.antialiasing = false;
        child.strokes.append(stroke);

        Layer group;
        group.name = QStringLiteral("Group");
        group.kind = LayerKind::Group;
        group.opacity = 0.5;
        group.initialCanvasSize = document.size;
        child.parentGroupId = group.id;
        document.layers.append(group);

        const QImage rendered = RenderEngine::render(document, 0);
        QVERIFY(!rendered.isNull());
        const QColor center = rendered.pixelColor(12, 12);
        QVERIFY(std::abs(center.red() - 227) <= 1);
        QVERIFY(std::abs(center.green() - 137) <= 1);
        QVERIFY(std::abs(center.blue() - 147) <= 1);

        const RenderEngine::LayerSplitFrame split =
            RenderEngine::renderLayerSplit(
                document, 0, document.size, child.id);
        QVERIFY(!split.valid);
    }

    void cachesHierarchicalLayerRastersForInteraction()
    {
        Document document = Document::createDefault(QSize(48, 48));
        document.background = Qt::white;
        document.wobbleAmount = 0.0;
        Layer &base = document.layers.first();
        base.strokes.append(makeStroke(StrokeMode::Paint,
            QColor(220, 50, 60),
            20.0,
            1,
            {QPointF(24.0, 24.0)}));

        Layer clipped;
        clipped.name = QStringLiteral("Clipped");
        clipped.initialCanvasSize = document.size;
        clipped.clipToLayerBelow = true;
        clipped.opacity = 0.75;
        clipped.blendMode = LayerBlendMode::Overlay;
        clipped.strokes.append(makeStroke(StrokeMode::Paint,
            QColor(40, 90, 220),
            8.0,
            2,
            {QPointF(24.0, 24.0)}));

        Layer group;
        group.name = QStringLiteral("Group");
        group.kind = LayerKind::Group;
        group.initialCanvasSize = document.size;
        group.opacity = 0.65;
        base.parentGroupId = group.id;
        clipped.parentGroupId = group.id;
        const QUuid clippedId = clipped.id;
        document.layers.append(clipped);
        document.layers.append(group);

        RenderEngine::ScaledRenderStats stats;
        const RenderEngine::LayerRasterFrame frame =
            RenderEngine::renderLayerRasterFrame(document,
                0,
                document.size,
                4 * 1024 * 1024,
                RenderEngine::ScaledRenderMode::DisplayPreview,
                &stats);
        QVERIFY(frame.valid);
        QCOMPARE(frame.paintLayers.size(), 2);

        Stroke active = makeStroke(StrokeMode::Paint,
            QColor(40, 180, 90),
            6.0,
            3,
            {QPointF(24.0, 24.0)});
        Document expected = document;
        expected.layer(clippedId)->strokes.append(active);
        QImage activeLayer = frame.paintLayers.value(clippedId);
        QVERIFY(RenderEngine::renderStrokesOnLayer(
            activeLayer, document, {active}, 0, document.size));
        QCOMPARE(RenderEngine::composeLayerRasterFrame(
                     document, frame, clippedId, activeLayer),
            RenderEngine::render(expected, 0));

        const QSize previewSize(31, 29);
        const RenderEngine::LayerRasterFrame previewFrame =
            RenderEngine::renderLayerRasterFrame(document,
                0,
                previewSize,
                4 * 1024 * 1024,
                RenderEngine::ScaledRenderMode::DisplayPreview);
        QVERIFY(previewFrame.valid);
        QImage fullPreviewLayer = previewFrame.paintLayers.value(clippedId);
        QVERIFY(RenderEngine::renderStrokesOnLayer(
            fullPreviewLayer, document, {active}, 0, previewSize));
        const QImage fullPreview = RenderEngine::composeLayerRasterFrame(
            document, previewFrame, clippedId, fullPreviewLayer);
        QCOMPARE(
            fullPreview, RenderEngine::renderScaled(expected, 0, previewSize));

        const QRect previewBounds =
            RenderEngine::strokePreviewBounds(document, active, previewSize);
        QVERIFY(!previewBounds.isEmpty());
        QVERIFY(previewBounds != QRect(QPoint(), previewSize));
        QImage regionalLayer =
            previewFrame.paintLayers.value(clippedId).copy(previewBounds);
        QVERIFY(RenderEngine::renderStrokesOnLayerRegion(
            regionalLayer, document, {active}, 0, previewSize, previewBounds));
        QCOMPARE(RenderEngine::composeLayerRasterFrameRegion(document,
                     previewFrame,
                     clippedId,
                     regionalLayer,
                     previewBounds),
            fullPreview.copy(previewBounds));
        QCOMPARE(RenderEngine::composeLayerRasterFrameRegion(document,
                     previewFrame,
                     clippedId,
                     fullPreviewLayer,
                     QRect(QPoint(), previewSize)),
            fullPreview);

        const RenderEngine::LayerRasterFrame constrained =
            RenderEngine::renderLayerRasterFrame(document, 0, document.size, 1);
        QVERIFY(!constrained.valid);
    }

    void identifiesOnlyEditableStrokesIntersectingSelection()
    {
        Document document = Document::createDefault(QSize(96, 64));
        document.background = Qt::transparent;
        document.wobbleAmount = 0.0;
        Layer &layer = document.layers.first();
        const Stroke left = makeStroke(StrokeMode::Paint,
            QColor(210, 40, 60),
            10.0,
            1,
            {QPointF(12.0, 32.0), QPointF(36.0, 32.0)});
        const Stroke right = makeStroke(StrokeMode::Paint,
            QColor(40, 80, 220),
            10.0,
            2,
            {QPointF(60.0, 32.0), QPointF(84.0, 32.0)});
        layer.strokes = {left, right};
        const QImage selection =
            rectangularMask(document.size, QRect(4, 20, 40, 24));

        const QVector<QUuid> ids = SelectionVisibility::editableStrokeIds(
            document, layer, selection, 0);
        QCOMPARE(ids, QVector<QUuid>{left.id});
    }

    void preservesEditableStrokeVisibilityThroughFramebufferOperations()
    {
        Document movedDocument = Document::createDefault(QSize(96, 64));
        movedDocument.background = Qt::transparent;
        movedDocument.wobbleAmount = 0.0;
        Layer &movedLayer = movedDocument.layers.first();
        Stroke moved = makeStroke(StrokeMode::Paint,
            QColor(210, 40, 60),
            16.0,
            11,
            {QPointF(16.0, 24.0)});
        moved.brush.tipShape = BrushTipShape::Square;
        moved.brush.sizeDynamics = 0.0;
        movedLayer.strokes.append(moved);
        const QImage sourceSelection =
            rectangularMask(movedDocument.size, QRect(8, 16, 16, 16));
        QTransform shift;
        shift.translate(48.0, 16.0);
        const std::optional<PixelSelectionOp> moveOperation =
            makePixelSelectionOp(sourceSelection, shift, true, true);
        QVERIFY(moveOperation.has_value());
        Stroke move;
        move.mode = StrokeMode::PixelSelection;
        move.pixelSelectionOp = *moveOperation;
        movedLayer.strokes.append(move);

        QCOMPARE(SelectionVisibility::editableStrokeIds(
                     movedDocument, movedLayer, sourceSelection, 0),
            QVector<QUuid>());
        const QImage destinationSelection =
            rectangularMask(movedDocument.size, QRect(56, 32, 16, 16));
        QCOMPARE(SelectionVisibility::editableStrokeIds(
                     movedDocument, movedLayer, destinationSelection, 0),
            QVector<QUuid>{moved.id});

        Document erasedDocument = Document::createDefault(QSize(64, 64));
        erasedDocument.background = Qt::transparent;
        erasedDocument.wobbleAmount = 0.0;
        Layer &erasedLayer = erasedDocument.layers.first();
        Stroke painted = makeStroke(StrokeMode::Paint,
            QColor(40, 90, 220),
            20.0,
            21,
            {QPointF(32.0, 32.0)});
        painted.brush.tipShape = BrushTipShape::Square;
        painted.brush.sizeDynamics = 0.0;
        Stroke erased = painted;
        erased.id = QUuid::createUuid();
        erased.mode = StrokeMode::Erase;
        erased.seed = 22;
        erasedLayer.strokes = {painted, erased};
        const QImage erasedSelection =
            rectangularMask(erasedDocument.size, QRect(28, 28, 8, 8));
        QCOMPARE(SelectionVisibility::editableStrokeIds(
                     erasedDocument, erasedLayer, erasedSelection, 0),
            QVector<QUuid>{erased.id});

        Document reframedDocument = Document::createDefault(QSize(96, 72));
        reframedDocument.background = Qt::transparent;
        reframedDocument.wobbleAmount = 0.0;
        Layer &reframedLayer = reframedDocument.layers.first();
        reframedLayer.initialCanvasSize = QSize(128, 96);
        Stroke reframed = makeStroke(StrokeMode::Paint,
            QColor(35, 170, 95),
            16.0,
            31,
            {QPointF(32.0, 32.0)});
        reframed.brush.tipShape = BrushTipShape::Square;
        reframed.brush.sizeDynamics = 0.0;
        reframedLayer.strokes.append(reframed);
        Stroke crop;
        crop.mode = StrokeMode::Reframe;
        crop.reframeOp = ReframeOp{ReframeMode::Canvas,
            SamplingMode::Nearest,
            QSize(128, 96),
            reframedDocument.size,
            QPoint(-16, -8)};
        reframedLayer.strokes.append(crop);
        const QImage reframedSelection =
            rectangularMask(reframedDocument.size, QRect(12, 20, 8, 8));
        QCOMPARE(SelectionVisibility::editableStrokeIds(
                     reframedDocument, reframedLayer, reframedSelection, 0),
            QVector<QUuid>{reframed.id});
    }

    void matchesSparseCoverageToLegacyFramebufferReplay()
    {
        const auto verify = [](const Document &document, int strokeIndex)
        {
            const Layer &layer = document.layers.first();
            const RenderEngine::StrokeCoveragePlan plan =
                RenderEngine::prepareStrokeCoverage(document, layer);
            QVERIFY(plan.valid);
            RenderEngine::StrokeCoverageStats stats;
            const RenderEngine::StrokeCoverageRegion sparse =
                RenderEngine::renderSparseStrokeCoverage(document,
                    layer,
                    strokeIndex,
                    0,
                    QRect(QPoint(), document.size),
                    plan,
                    &stats);
            QVERIFY(sparse.valid);
            QCOMPARE(expandedStrokeCoverage(sparse, document.size),
                RenderEngine::renderStrokeCoverage(
                    document, layer, strokeIndex, 0));
            QCOMPARE(stats.fullCanvasFallbacks, 0ULL);
            QCOMPARE(stats.regionalRenders, 1ULL);
        };

        Document moved = Document::createDefault(QSize(96, 72));
        moved.background = Qt::transparent;
        moved.wobbleAmount = 0.0;
        Layer &movedLayer = moved.layers.first();
        Stroke source = makeStroke(StrokeMode::Paint,
            QColor(210, 45, 80),
            18.0,
            1,
            {QPointF(18.0, 20.0), QPointF(42.0, 32.0)});
        movedLayer.strokes.append(source);
        const QImage sourceMask =
            rectangularMask(moved.size, QRect(8, 8, 44, 36));
        QTransform transform;
        transform.translate(24.0, 12.0);
        transform.rotate(11.0);
        transform.scale(0.9, 1.1);
        const std::optional<PixelSelectionOp> pixelOperation =
            makePixelSelectionOp(sourceMask, transform, true, true);
        QVERIFY(pixelOperation.has_value());
        QCOMPARE(pixelOperation->sampling, SamplingMode::Smooth);
        Stroke pixel;
        pixel.mode = StrokeMode::PixelSelection;
        pixel.pixelSelectionOp = *pixelOperation;
        movedLayer.strokes.append(pixel);
        Stroke erase = makeStroke(StrokeMode::Erase,
            Qt::black,
            7.0,
            2,
            {QPointF(55.0, 34.0), QPointF(68.0, 42.0)});
        erase.clipMask = rectangularMask(moved.size, QRect(48, 24, 28, 28));
        movedLayer.strokes.append(erase);
        verify(moved, 0);

        Document copied = moved;
        copied.layers.first().strokes.removeLast();
        copied.layers.first().strokes[1].pixelSelectionOp->clearSource = false;
        verify(copied, 0);

        Document cleared = copied;
        PixelSelectionOp &clearOperation =
            *cleared.layers.first().strokes[1].pixelSelectionOp;
        clearOperation.clearSource = true;
        clearOperation.drawDestination = false;
        verify(cleared, 0);

        Document nearest = moved;
        nearest.layers.first().strokes.removeLast();
        PixelSelectionOp &nearestOperation =
            *nearest.layers.first().strokes[1].pixelSelectionOp;
        nearestOperation.transform = QTransform::fromTranslate(24.0, 12.0);
        nearestOperation.sampling = SamplingMode::Nearest;
        verify(nearest, 0);

        Document canvasReframe = Document::createDefault(QSize(96, 72));
        canvasReframe.background = Qt::transparent;
        canvasReframe.wobbleAmount = 0.0;
        Layer &canvasLayer = canvasReframe.layers.first();
        canvasLayer.initialCanvasSize = QSize(128, 96);
        canvasLayer.strokes.append(makeStroke(StrokeMode::Paint,
            QColor(40, 170, 100),
            20.0,
            3,
            {QPointF(30.0, 30.0), QPointF(72.0, 52.0)}));
        Stroke crop;
        crop.mode = StrokeMode::Reframe;
        crop.reframeOp = ReframeOp{ReframeMode::Canvas,
            SamplingMode::Nearest,
            QSize(128, 96),
            canvasReframe.size,
            QPoint(-16, -8)};
        canvasLayer.strokes.append(crop);
        verify(canvasReframe, 0);

        Document imageReframe = Document::createDefault(QSize(79, 61));
        imageReframe.background = Qt::transparent;
        imageReframe.wobbleAmount = 0.0;
        Layer &imageLayer = imageReframe.layers.first();
        imageLayer.initialCanvasSize = QSize(97, 73);
        imageLayer.strokes.append(makeStroke(StrokeMode::Paint,
            QColor(60, 100, 220),
            17.0,
            4,
            {QPointF(21.0, 18.0), QPointF(66.0, 51.0)}));
        Stroke resize;
        resize.mode = StrokeMode::Reframe;
        resize.reframeOp = ReframeOp{ReframeMode::Image,
            SamplingMode::Smooth,
            QSize(97, 73),
            imageReframe.size,
            {}};
        imageLayer.strokes.append(resize);
        verify(imageReframe, 0);

        Document nearestImageReframe = imageReframe;
        nearestImageReframe.layers.first().strokes.last().reframeOp->sampling =
            SamplingMode::Nearest;
        verify(nearestImageReframe, 0);

        Document maskedFill = Document::createDefault(QSize(96, 72));
        maskedFill.background = Qt::transparent;
        Stroke fill;
        fill.mode = StrokeMode::Fill;
        fill.color = QColor(255, 180, 40, 117);
        fill.points = {{QPointF(12.0, 18.0), 1.0}};
        fill.fillMask = rectangularMask(maskedFill.size, QRect(17, 13, 29, 21));
        fill.clipMask = rectangularMask(maskedFill.size, QRect(20, 11, 31, 19));
        fill.visibilityClip = QRect(19, 12, 24, 23);
        maskedFill.layers.first().strokes = {fill};
        verify(maskedFill, 0);

        Document proceduralFill = Document::createDefault(QSize(96, 72));
        proceduralFill.background = Qt::transparent;
        fill.fillMask = {};
        fill.clipMask =
            rectangularMask(proceduralFill.size, QRect(8, 9, 37, 31));
        fill.visibilityClip = QRect(11, 7, 28, 29);
        proceduralFill.layers.first().strokes = {fill};
        verify(proceduralFill, 0);
    }

    void resamplesImageRegionsDeterministicallyNearQtReference()
    {
        QRandomGenerator random(0x7391U);
        QImage source(QSize(73, 47), QImage::Format_ARGB32_Premultiplied);
        QVERIFY(!source.isNull());
        for (int y = 0; y < source.height(); ++y)
        {
            auto *line = reinterpret_cast<QRgb *>(source.scanLine(y));
            for (int x = 0; x < source.width(); ++x)
            {
                const int alpha = int(random.bounded(256U));
                line[x] = qRgba(random.bounded(alpha + 1),
                    random.bounded(alpha + 1),
                    random.bounded(alpha + 1),
                    alpha);
            }
        }

        const QSize targetSize(119, 61);
        for (const SamplingMode sampling :
            {SamplingMode::Nearest, SamplingMode::Smooth})
        {
            const QImage full =
                ImageResampler::resample(source, targetSize, sampling);
            QVERIFY(!full.isNull());
            for (const QRect &region :
                {QRect(0, 0, 1, 1),
                    QRect(17, 9, 43, 21),
                    QRect(
                        targetSize.width() - 1, targetSize.height() - 1, 1, 1)})
            {
                QCOMPARE(ImageResampler::resampleRegion(source,
                             source.rect(),
                             source.size(),
                             region,
                             targetSize,
                             sampling),
                    full.copy(region));
            }
        }

        QImage singlePixel(1, 1, QImage::Format_ARGB32_Premultiplied);
        singlePixel.setPixel(0, 0, qRgba(17, 31, 47, 63));
        const QRect farEdge(4095, 4095, 1, 1);
        for (const SamplingMode sampling :
            {SamplingMode::Nearest, SamplingMode::Smooth})
        {
            const QImage edge = ImageResampler::resampleRegion(singlePixel,
                singlePixel.rect(),
                singlePixel.size(),
                farEdge,
                QSize(4096, 4096),
                sampling);
            QCOMPARE(edge.size(), QSize(1, 1));
            QCOMPARE(edge.pixel(0, 0), singlePixel.pixel(0, 0));
        }

        const QImage deterministic =
            ImageResampler::resample(source, targetSize, SamplingMode::Smooth);
        const QImage qtReference =
            qtResizedRasterResult(source, targetSize, true);
        const ImageDifference difference =
            storedImageDifference(deterministic, qtReference, 0);
        qInfo("deterministic image resize differs from Qt by %llu total "
              "channel levels, %.4f average, max %d",
            static_cast<unsigned long long>(difference.channelDifference),
            double(difference.channelDifference)
                / double(difference.comparedChannels),
            difference.maximumChannelDifference);
        QVERIFY(difference.maximumChannelDifference <= 2);
        QVERIFY(difference.channelDifference * 100
                <= difference.comparedChannels * 6);
    }

    void transformsAffineImagesExactlyAcrossFullAndRegionalTargets()
    {
        QRandomGenerator random(0x7391U);
        const QSize canvasSize(64, 48);
        const QRect canvasBounds(QPoint(), canvasSize);
        for (int caseIndex = 0; caseIndex < 500; ++caseIndex)
        {
            const int sourceWidth = 6 + int(random.bounded(18U));
            const int sourceHeight = 6 + int(random.bounded(18U));
            const QRect sourceBounds(
                int(random.bounded(quint32(canvasSize.width() - sourceWidth))),
                int(random.bounded(
                    quint32(canvasSize.height() - sourceHeight))),
                sourceWidth,
                sourceHeight);
            const QRect contentBounds = sourceBounds.adjusted(2, 2, -2, -2);
            const QRect croppedBounds = contentBounds.adjusted(-1, -1, 1, 1);
            QImage source(
                sourceBounds.size(), QImage::Format_ARGB32_Premultiplied);
            source.fill(Qt::transparent);
            for (int y = contentBounds.top(); y <= contentBounds.bottom(); ++y)
            {
                auto *line = reinterpret_cast<QRgb *>(
                    source.scanLine(y - sourceBounds.top()));
                for (int x = contentBounds.left(); x <= contentBounds.right();
                    ++x)
                {
                    const int alpha = 1 + int(random.bounded(255U));
                    line[x - sourceBounds.left()] =
                        qRgba(random.bounded(alpha + 1),
                            random.bounded(alpha + 1),
                            random.bounded(alpha + 1),
                            alpha);
                }
            }
            QImage base(canvasSize, QImage::Format_ARGB32_Premultiplied);
            for (int y = 0; y < base.height(); ++y)
            {
                auto *line = reinterpret_cast<QRgb *>(base.scanLine(y));
                for (int x = 0; x < base.width(); ++x)
                {
                    const int alpha = int(random.bounded(256U));
                    line[x] = qRgba(random.bounded(alpha + 1),
                        random.bounded(alpha + 1),
                        random.bounded(alpha + 1),
                        alpha);
                }
            }

            QTransform transform;
            const QPointF center = sourceBounds.center();
            switch (caseIndex % 6)
            {
            case 0:
                transform.translate(
                    int(random.bounded(17U)) - 8, int(random.bounded(17U)) - 8);
                break;
            case 1:
                transform.translate(qreal(int(random.bounded(17U)) - 8) + 0.375,
                    qreal(int(random.bounded(17U)) - 8) - 0.4375);
                break;
            case 2:
                transform.translate(center.x(), center.y());
                transform.rotate(qreal(int(random.bounded(61U)) - 30));
                transform.translate(-center.x(), -center.y());
                break;
            case 3:
                transform.translate(center.x(), center.y());
                transform.shear(0.08 * (int(random.bounded(9U)) - 4),
                    0.06 * (int(random.bounded(9U)) - 4));
                transform.scale(0.65 + random.generateDouble(),
                    0.65 + random.generateDouble());
                transform.translate(-center.x(), -center.y());
                break;
            case 4:
                transform.translate(
                    caseIndex % 2 == 0 ? 1.0 - 1.0e-12 : 1.0 + 1.0e-12,
                    caseIndex % 4 == 0 ? -1.0e-12 : 1.0e-12);
                break;
            case 5:
                transform.translate(center.x(), center.y());
                transform.rotate(-17.0);
                transform.shear(0.13, -0.09);
                transform.scale(1.21, 0.83);
                transform.translate(-center.x(), -center.y());
                break;
            }
            QVERIFY(transform.isInvertible());
            const SamplingMode sampling = caseIndex % 3 == 0
                                              ? SamplingMode::Nearest
                                              : SamplingMode::Smooth;

            QImage full = base;
            QVERIFY(ImageAffineTransformer::compositeSourceOver(
                full, canvasBounds, source, sourceBounds, transform, sampling));

            QImage assembled = base;
            const QImage cropped =
                source.copy(croppedBounds.translated(-sourceBounds.topLeft()));
            const QRect effectBounds = ImageAffineTransformer::targetBounds(
                croppedBounds, canvasSize, transform, sampling);
            if (!effectBounds.isEmpty())
            {
                QImage regional = base.copy(effectBounds);
                QVERIFY(ImageAffineTransformer::compositeSourceOver(regional,
                    effectBounds,
                    cropped,
                    croppedBounds,
                    transform,
                    sampling));
                QPainter painter(&assembled);
                painter.setCompositionMode(QPainter::CompositionMode_Source);
                painter.drawImage(effectBounds.topLeft(), regional);
                painter.end();
            }
            QCOMPARE(full, assembled);
        }
    }

    void keepsAffineSelectionSupportAlignedAtHalfAlpha()
    {
        const QSize canvasSize(72, 56);
        const QImage selection =
            rectangularMask(canvasSize, QRect(11, 9, 27, 19));
        const QRect sourceBounds(11, 9, 27, 19);
        QImage payload(
            sourceBounds.size(), QImage::Format_ARGB32_Premultiplied);
        payload.fill(Qt::white);
        const QVector<QTransform> transforms = {
            QTransform::fromTranslate(7.0, -3.0),
            QTransform::fromTranslate(7.375, -3.4375),
            QTransform(1.15, -0.17, 0.12, 0.83, 4.0, 3.0),
            QTransform(0.91, 0.26, -0.19, 1.08, -2.0, 5.0)};
        int falsePositives = 0;
        int falseNegatives = 0;
        for (qsizetype transformIndex = 0; transformIndex < transforms.size();
            ++transformIndex)
        {
            const QTransform &transform = transforms[transformIndex];
            const SamplingMode sampling =
                samplingForSelectionTransform(transform);
            QImage transformed(canvasSize, QImage::Format_ARGB32_Premultiplied);
            transformed.fill(Qt::transparent);
            QVERIFY(ImageAffineTransformer::compositeSourceOver(transformed,
                QRect(QPoint(), canvasSize),
                payload,
                sourceBounds,
                transform,
                sampling));
            const QImage support = transformedSelectionSupport(
                selection, canvasSize, transform, sampling);
            QVERIFY(!support.isNull());
            for (int y = 0; y < canvasSize.height(); ++y)
            {
                const auto *transformedLine = reinterpret_cast<const QRgb *>(
                    transformed.constScanLine(y));
                const uchar *supportLine = support.constScanLine(y);
                for (int x = 0; x < canvasSize.width(); ++x)
                {
                    const bool transformedContains =
                        qAlpha(transformedLine[x]) >= 128;
                    const bool supportContains = supportLine[x] >= 128;
                    falsePositives +=
                        !transformedContains && supportContains ? 1 : 0;
                    falseNegatives +=
                        transformedContains && !supportContains ? 1 : 0;
                }
            }
        }
        qInfo("selection support differs from deterministic payload at >=128 "
              "by %d false positives and %d false negatives",
            falsePositives,
            falseNegatives);
        QCOMPARE(falsePositives, 0);
        QCOMPARE(falseNegatives, 0);
    }

    void keepsAffineTransformNearQtReference()
    {
        const QSize canvasSize(80, 64);
        const QRect canvasBounds(QPoint(), canvasSize);
        const QRect sourceBounds(17, 13, 23, 19);
        QImage source(sourceBounds.size(), QImage::Format_ARGB32_Premultiplied);
        source.fill(Qt::transparent);
        QRandomGenerator random(0x2814U);
        for (int y = 1; y < source.height() - 1; ++y)
        {
            auto *line = reinterpret_cast<QRgb *>(source.scanLine(y));
            for (int x = 1; x < source.width() - 1; ++x)
            {
                const int alpha = int(random.bounded(256U));
                line[x] = qRgba(random.bounded(alpha + 1),
                    random.bounded(alpha + 1),
                    random.bounded(alpha + 1),
                    alpha);
            }
        }
        QImage base(canvasSize, QImage::Format_ARGB32_Premultiplied);
        for (int y = 0; y < base.height(); ++y)
        {
            auto *line = reinterpret_cast<QRgb *>(base.scanLine(y));
            for (int x = 0; x < base.width(); ++x)
            {
                const int alpha = int(random.bounded(256U));
                line[x] = qRgba(random.bounded(alpha + 1),
                    random.bounded(alpha + 1),
                    random.bounded(alpha + 1),
                    alpha);
            }
        }
        const QVector<QTransform> transforms = {
            QTransform::fromTranslate(9.0, -4.0),
            QTransform::fromTranslate(9.375, -4.4375),
            QTransform(1.21, -0.14, 0.09, 0.82, 3.0, 6.0),
            QTransform(0.88, 0.31, -0.22, 1.13, -4.0, 2.0)};
        quint64 totalDifference = 0;
        quint64 comparedChannels = 0;
        qsizetype differentPixels = 0;
        qsizetype comparedPixels = 0;
        int maximumDifference = 0;
        for (qsizetype transformIndex = 0; transformIndex < transforms.size();
            ++transformIndex)
        {
            const QTransform &transform = transforms[transformIndex];
            const SamplingMode sampling =
                samplingForSelectionTransform(transform);
            QImage deterministic = base;
            QVERIFY(ImageAffineTransformer::compositeSourceOver(deterministic,
                canvasBounds,
                source,
                sourceBounds,
                transform,
                sampling));
            const QImage qtReference = qtAffineComposite(
                base, canvasBounds, source, sourceBounds, transform, sampling);
            const ImageDifference difference =
                storedImageDifference(deterministic, qtReference, 0);
            totalDifference += difference.channelDifference;
            comparedChannels += difference.comparedChannels;
            differentPixels += difference.visiblyDifferentPixels;
            comparedPixels += canvasSize.width() * canvasSize.height();
            maximumDifference = std::max(
                maximumDifference, difference.maximumChannelDifference);
        }
        qInfo("deterministic affine transform differs from Qt by %llu total "
              "channel levels, %.4f average, across %lld/%lld pixels, max %d",
            static_cast<unsigned long long>(totalDifference),
            double(totalDifference) / double(comparedChannels),
            static_cast<long long>(differentPixels),
            static_cast<long long>(comparedPixels),
            maximumDifference);
        QVERIFY(maximumDifference <= 10);
        QVERIFY(totalDifference * 100 <= comparedChannels * 5);
        QVERIFY(differentPixels * 100 <= comparedPixels * 4);
    }

    void filtersLargeStrokeSetsBeforeRenderingSelectionCoverage()
    {
        constexpr int edge = DocumentLimits::maximumCanvasEdge;
        constexpr int strokeCount = 12000;
        Document document = Document::createDefault(QSize(edge, edge));
        document.background = Qt::transparent;
        document.wobbleAmount = 0.0;
        Layer &layer = document.layers.first();
        layer.strokes.reserve(strokeCount + 1);
        for (int index = 0; index < strokeCount; ++index)
        {
            layer.strokes.append(makeStroke(StrokeMode::Paint,
                Qt::black,
                2.0,
                static_cast<quint64>(index + 1),
                {QPointF(32.0 + index % 128, 32.0 + index / 128 % 128)}));
        }
        const Stroke target = makeStroke(StrokeMode::Paint,
            QColor(220, 50, 80),
            8.0,
            0xfedcba98ULL,
            {QPointF(4000.0, 4000.0)});
        layer.strokes.append(target);
        const QRect selectionBounds(3996, 3996, 9, 9);
        const QImage selection =
            rectangularMask(document.size, selectionBounds);

        QElapsedTimer timer;
        timer.start();
        const QVector<QUuid> ids = SelectionVisibility::editableStrokeIds(
            document, layer, selection, 0);
        const qint64 elapsed = timer.elapsed();

        QCOMPARE(ids, QVector<QUuid>{target.id});
        QVERIFY2(elapsed < 5000,
            qPrintable(QStringLiteral("editable stroke lookup took %1 ms")
                    .arg(elapsed)));
        const QImage coverage = RenderEngine::renderStrokeCoverageRegion(
            document, layer, layer.strokes.size() - 1, 0, selectionBounds);
        QCOMPARE(coverage.size(), selectionBounds.size());
    }

    void boundsPerStrokeCoverageForFullCanvasSelection()
    {
        constexpr int edge = DocumentLimits::maximumCanvasEdge;
        constexpr int strokeCount = 1024;
        Document document = Document::createDefault(QSize(edge, edge));
        document.background = Qt::transparent;
        document.wobbleAmount = 0.0;
        Layer &layer = document.layers.first();
        layer.strokes.reserve(strokeCount);
        QVector<QUuid> expectedIds;
        expectedIds.reserve(strokeCount);
        for (int index = 0; index < strokeCount; ++index)
        {
            Stroke stroke = makeStroke(StrokeMode::Paint,
                Qt::black,
                2.0,
                static_cast<quint64>(index + 1),
                {QPointF(32.0 + index % 32 * 24, 32.0 + index / 32 * 24)});
            expectedIds.append(stroke.id);
            layer.strokes.append(std::move(stroke));
        }
        QImage selection(document.size, QImage::Format_Grayscale8);
        selection.fill(255);

        QElapsedTimer timer;
        timer.start();
        const QVector<QUuid> ids = SelectionVisibility::editableStrokeIds(
            document, layer, selection, 0);
        const qint64 elapsed = timer.elapsed();

        QCOMPARE(ids, expectedIds);
        QVERIFY2(elapsed < 5000,
            qPrintable(QStringLiteral("full selection lookup took %1 ms")
                    .arg(elapsed)));
    }

    void boundsMaskedAndProceduralFillCoverage()
    {
        constexpr int edge = DocumentLimits::maximumCanvasEdge;
        Document document = Document::createDefault(QSize(edge, edge));
        document.background = Qt::transparent;
        Stroke fill;
        fill.mode = StrokeMode::Fill;
        fill.color = QColor(210, 90, 35, 173);
        fill.points = {{QPointF(24.0, 24.0), 1.0}};
        fill.fillMask = rectangularMask(document.size, QRect(12, 14, 64, 48));
        document.layers.first().strokes = {fill};
        QImage selection =
            rectangularMask(document.size, QRect(20, 20, 24, 20));

        SelectionVisibility::EditableStrokeStats stats;
        QCOMPARE(SelectionVisibility::editableStrokeIds(
                     document, document.layers.first(), selection, 0, &stats),
            QVector<QUuid>{fill.id});
        QCOMPARE(stats.fullCanvasFallbacks, 0ULL);
        QVERIFY(stats.maximumExplicitImageBytes < 1024ULL * 1024ULL);

        document.layers.first().strokes.first().fillMask = {};
        stats = {};
        QCOMPARE(SelectionVisibility::editableStrokeIds(
                     document, document.layers.first(), selection, 0, &stats),
            QVector<QUuid>{fill.id});
        QCOMPARE(stats.fullCanvasFallbacks, 0ULL);
        QVERIFY(stats.maximumExplicitImageBytes < 1024ULL * 1024ULL);
    }

    void reportsFarCopyCoverageImageBound()
    {
        constexpr int edge = DocumentLimits::maximumCanvasEdge;
        Document document = Document::createDefault(QSize(edge, edge));
        document.background = Qt::transparent;
        document.wobbleAmount = 0.0;
        Stroke paint = makeStroke(
            StrokeMode::Paint, Qt::black, 8.0, 91, {QPointF(24.0, 24.0)});
        paint.brush.antialiasing = false;
        PixelSelectionOp copy;
        copy.canvasSize = document.size;
        copy.sourceBounds = QRect(0, 0, 64, 64);
        copy.packedMask = QByteArray(8 * 64, std::bit_cast<char>(quint8{0xff}));
        copy.transform = QTransform::fromTranslate(3960.0, 3960.0);
        copy.sampling = SamplingMode::Nearest;
        copy.clearSource = false;
        copy.drawDestination = true;
        QVERIFY(isValidPixelSelectionOp(copy));
        Stroke operation;
        operation.mode = StrokeMode::PixelSelection;
        operation.pixelSelectionOp = copy;
        document.layers.first().strokes = {paint, operation};

        const RenderEngine::StrokeCoveragePlan plan =
            RenderEngine::prepareStrokeCoverage(
                document, document.layers.first());
        QVERIFY(plan.valid);
        RenderEngine::StrokeCoverageStats stats;
        QElapsedTimer timer;
        timer.start();
        const RenderEngine::StrokeCoverageRegion coverage =
            RenderEngine::renderSparseStrokeCoverage(document,
                document.layers.first(),
                0,
                0,
                QRect(3960, 3960, 96, 96),
                plan,
                &stats);
        const qint64 elapsed = timer.elapsed();
        qInfo("4K far-copy coverage took %lld ms, max image %llu bytes",
            static_cast<long long>(elapsed),
            static_cast<unsigned long long>(stats.maximumExplicitImageBytes));
        QVERIFY(coverage.valid);
        QVERIFY(!coverage.image.isNull());
        QCOMPARE(stats.fullCanvasFallbacks, 0ULL);
        QVERIFY(stats.maximumExplicitImageBytes <= 64ULL * 1024ULL * 1024ULL);
        QVERIFY(elapsed < 10000);
    }

    void benchmarksFullCanvasSelectionAfterFramebufferOperation()
    {
        constexpr int edge = DocumentLimits::maximumCanvasEdge;
        constexpr int strokeCount = DocumentLimits::maximumStrokesPerLayer - 1;
        Document document = Document::createDefault(QSize(edge, edge));
        document.background = Qt::transparent;
        document.wobbleAmount = 0.0;
        Layer &layer = document.layers.first();
        layer.strokes.reserve(strokeCount + 1);
        QVector<QUuid> expectedIds;
        expectedIds.reserve(strokeCount);
        for (int index = 0; index < strokeCount; ++index)
        {
            Stroke stroke = makeStroke(StrokeMode::Paint,
                Qt::black,
                2.0,
                static_cast<quint64>(index + 1),
                {QPointF(
                    32.0 + index % 128 * 31, 32.0 + index / 128 % 128 * 31)});
            expectedIds.append(stroke.id);
            layer.strokes.append(std::move(stroke));
        }
        QImage selection(document.size, QImage::Format_Grayscale8);
        selection.fill(255);
        const std::optional<PixelSelectionOp> pixelOperation =
            makePixelSelectionOp(selection, QTransform(), true, true);
        QVERIFY(pixelOperation.has_value());
        Stroke operation;
        operation.mode = StrokeMode::PixelSelection;
        operation.pixelSelectionOp = *pixelOperation;
        layer.strokes.append(std::move(operation));

        QElapsedTimer timer;
        timer.start();
        SelectionVisibility::EditableStrokeStats stats;
        const QVector<QUuid> ids = SelectionVisibility::editableStrokeIds(
            document, layer, selection, 0, &stats);
        const qint64 elapsed = timer.elapsed();

        qInfo("19999-stroke pixel-selection lookup took %lld ms, %llu "
              "operations, %llu effects, max image %llu bytes",
            static_cast<long long>(elapsed),
            static_cast<unsigned long long>(
                stats.pixelSelectionOperationsReplayed),
            static_cast<unsigned long long>(stats.effectCandidatesExamined),
            static_cast<unsigned long long>(stats.maximumExplicitImageBytes));
        QCOMPARE(ids, expectedIds);
        QCOMPARE(stats.fullCanvasFallbacks, 0ULL);
        QCOMPARE(stats.regionalRenders, quint64(strokeCount));
        QCOMPARE(stats.pixelSelectionOperationsReplayed, quint64(strokeCount));
        QCOMPARE(stats.reframeOperationsReplayed, 0ULL);
        QCOMPARE(stats.effectCandidatesExamined, quint64(strokeCount));
        QVERIFY(stats.maximumExplicitImageBytes < 1024ULL * 1024ULL);

        Document reframeDocument = Document::createDefault(QSize(edge, edge));
        reframeDocument.background = Qt::transparent;
        reframeDocument.wobbleAmount = 0.0;
        Layer &reframeLayer = reframeDocument.layers.first();
        reframeLayer.initialCanvasSize = QSize(edge - 1, edge - 1);
        reframeLayer.strokes = layer.strokes;
        reframeLayer.strokes.removeLast();
        Stroke reframe;
        reframe.mode = StrokeMode::Reframe;
        reframe.reframeOp = ReframeOp{ReframeMode::Image,
            SamplingMode::Smooth,
            QSize(edge - 1, edge - 1),
            reframeDocument.size,
            {}};
        reframeLayer.strokes.append(std::move(reframe));

        timer.restart();
        SelectionVisibility::EditableStrokeStats reframeStats;
        const QVector<QUuid> reframeIds =
            SelectionVisibility::editableStrokeIds(
                reframeDocument, reframeLayer, selection, 0, &reframeStats);
        const qint64 reframeElapsed = timer.elapsed();

        qInfo("19999-stroke image-reframe lookup took %lld ms, %llu "
              "operations, %llu effects, max image %llu bytes",
            static_cast<long long>(reframeElapsed),
            static_cast<unsigned long long>(
                reframeStats.reframeOperationsReplayed),
            static_cast<unsigned long long>(
                reframeStats.effectCandidatesExamined),
            static_cast<unsigned long long>(
                reframeStats.maximumExplicitImageBytes));
        QCOMPARE(reframeIds, expectedIds);
        QCOMPARE(reframeStats.fullCanvasFallbacks, 0ULL);
        QCOMPARE(reframeStats.regionalRenders, quint64(strokeCount));
        QCOMPARE(reframeStats.pixelSelectionOperationsReplayed, 0ULL);
        QCOMPARE(reframeStats.reframeOperationsReplayed, quint64(strokeCount));
        QCOMPARE(reframeStats.effectCandidatesExamined, quint64(strokeCount));
        QVERIFY(reframeStats.maximumExplicitImageBytes < 1024ULL * 1024ULL);
    }

    void benchmarksMixedPaintAndEraseCoverageEffects()
    {
        constexpr int edge = DocumentLimits::maximumCanvasEdge;
        constexpr int paintCount = 10000;
        constexpr int eraseCount = 10000;
        Document document = Document::createDefault(QSize(edge, edge));
        document.background = Qt::transparent;
        document.wobbleAmount = 0.0;
        Layer &layer = document.layers.first();
        layer.strokes.reserve(paintCount + eraseCount);
        for (int index = 0; index < paintCount; ++index)
        {
            layer.strokes.append(makeStroke(StrokeMode::Paint,
                Qt::black,
                2.0,
                static_cast<quint64>(index + 1),
                {QPointF(32.0 + index % 100 * 16, 32.0 + index / 100 * 16)}));
        }
        for (int index = 0; index < eraseCount; ++index)
        {
            layer.strokes.append(makeStroke(StrokeMode::Erase,
                Qt::black,
                2.0,
                static_cast<quint64>(paintCount + index + 1),
                {QPointF(2400.0 + index % 100 * 16, 32.0 + index / 100 * 16)}));
        }
        QImage selection(document.size, QImage::Format_Grayscale8);
        selection.fill(255);

        QElapsedTimer timer;
        timer.start();
        SelectionVisibility::EditableStrokeStats stats;
        const QVector<QUuid> ids = SelectionVisibility::editableStrokeIds(
            document, layer, selection, 0, &stats);
        const qint64 elapsed = timer.elapsed();

        qInfo("10000-paint/10000-erase lookup took %lld ms, %llu erases, "
              "%llu effects, max image %llu bytes",
            static_cast<long long>(elapsed),
            static_cast<unsigned long long>(stats.eraseOperationsReplayed),
            static_cast<unsigned long long>(stats.effectCandidatesExamined),
            static_cast<unsigned long long>(stats.maximumExplicitImageBytes));
        QCOMPARE(ids.size(), paintCount + eraseCount);
        QCOMPARE(stats.fullCanvasFallbacks, 0ULL);
        QCOMPARE(stats.regionalRenders, quint64(paintCount + eraseCount));
        QVERIFY(stats.eraseOperationsReplayed < 1000000ULL);
        QVERIFY(stats.effectCandidatesExamined < 2000000ULL);

        Document pixelDocument = Document::createDefault(QSize(edge, edge));
        pixelDocument.background = Qt::transparent;
        pixelDocument.wobbleAmount = 0.0;
        Layer &pixelLayer = pixelDocument.layers.first();
        pixelLayer.strokes.reserve(paintCount * 2);
        QVector<QUuid> expectedIds;
        expectedIds.reserve(paintCount);
        for (int index = 0; index < paintCount; ++index)
        {
            Stroke paint = makeStroke(StrokeMode::Paint,
                Qt::black,
                2.0,
                static_cast<quint64>(index + 1),
                {QPointF(32.0 + index % 100 * 16, 32.0 + index / 100 * 16)});
            expectedIds.append(paint.id);
            pixelLayer.strokes.append(std::move(paint));
            PixelSelectionOp pixelOperation;
            pixelOperation.canvasSize = pixelDocument.size;
            pixelOperation.sourceBounds =
                QRect(2400 + index % 100 * 16, 32 + index / 100 * 16, 1, 1);
            pixelOperation.packedMask =
                QByteArray(1, std::bit_cast<char>(quint8{0x80}));
            pixelOperation.sampling = SamplingMode::Nearest;
            QVERIFY(isValidPixelSelectionOp(pixelOperation));
            Stroke operation;
            operation.mode = StrokeMode::PixelSelection;
            operation.pixelSelectionOp = std::move(pixelOperation);
            pixelLayer.strokes.append(std::move(operation));
        }

        timer.restart();
        SelectionVisibility::EditableStrokeStats pixelStats;
        const QVector<QUuid> pixelIds = SelectionVisibility::editableStrokeIds(
            pixelDocument, pixelLayer, selection, 0, &pixelStats);
        const qint64 pixelElapsed = timer.elapsed();

        qInfo("10000-paint/10000-pixel-selection lookup took %lld ms, %llu "
              "operations, %llu effects, max image %llu bytes",
            static_cast<long long>(pixelElapsed),
            static_cast<unsigned long long>(
                pixelStats.pixelSelectionOperationsReplayed),
            static_cast<unsigned long long>(
                pixelStats.effectCandidatesExamined),
            static_cast<unsigned long long>(
                pixelStats.maximumExplicitImageBytes));
        QCOMPARE(pixelIds, expectedIds);
        QCOMPARE(pixelStats.fullCanvasFallbacks, 0ULL);
        QCOMPARE(pixelStats.regionalRenders, quint64(paintCount));
        QCOMPARE(pixelStats.pixelSelectionOperationsReplayed, 0ULL);
        QVERIFY(pixelStats.effectCandidatesExamined < 100000ULL);
    }

    void handlesBlendModesInLayerSplitPreviews()
    {
        Document document = Document::createDefault(QSize(48, 36));
        document.wobbleAmount = 0.0;
        Layer &target = document.layers.first();
        target.blendMode = LayerBlendMode::Overlay;
        target.strokes.append(makeStroke(StrokeMode::Paint,
            QColor(220, 70, 90),
            14.0,
            1,
            {QPointF(24.0, 18.0)}));
        const QUuid targetId = target.id;

        Layer top;
        top.name = QStringLiteral("Top");
        top.initialCanvasSize = document.size;
        top.strokes.append(makeStroke(StrokeMode::Paint,
            QColor(40, 180, 100),
            8.0,
            2,
            {QPointF(10.0, 10.0)}));
        document.layers.append(top);

        RenderEngine::LayerSplitFrame split = RenderEngine::renderLayerSplit(
            document, 0, document.size, targetId);
        QVERIFY(split.valid);
        QCOMPARE(RenderEngine::composeLayerSplit(split, split.layerBase),
            RenderEngine::render(document, 0));

        document.layers.last().blendMode = LayerBlendMode::Screen;
        split = RenderEngine::renderLayerSplit(
            document, 0, document.size, targetId);
        QVERIFY(!split.valid);
    }

    void composesLayerSplitLikeFullRender()
    {
        Document document = Document::createDefault(QSize(96, 72));
        document.animationFrames = 6;
        document.wobbleAmount = 3.0;
        document.layers.first().strokes.append(makeStroke(StrokeMode::Paint,
            QColor(200, 40, 40),
            8.0,
            11,
            {QPointF(10.0, 10.0), QPointF(80.0, 60.0)}));
        Layer middle;
        middle.name = QStringLiteral("Middle");
        middle.strokes.append(makeStroke(StrokeMode::Paint,
            QColor(40, 200, 40),
            7.0,
            22,
            {QPointF(10.0, 60.0), QPointF(80.0, 10.0)}));
        document.layers.append(middle);
        Layer top;
        top.name = QStringLiteral("Top");
        top.opacity = 0.65;
        top.strokes.append(makeStroke(StrokeMode::Paint,
            QColor(40, 40, 200),
            9.0,
            33,
            {QPointF(48.0, 6.0), QPointF(48.0, 66.0)}));
        document.layers.append(top);
        const QUuid middleId = document.layers[1].id;

        const QVector<Stroke> activeStrokes = {
            makeStroke(StrokeMode::Paint,
                QColor(250, 210, 60),
                5.0,
                44,
                {QPointF(20.0, 20.0), QPointF(70.0, 50.0)}),
            makeStroke(StrokeMode::Erase,
                QColor(Qt::black),
                12.0,
                55,
                {QPointF(30.0, 50.0), QPointF(60.0, 20.0)})};
        for (const Stroke &active : activeStrokes)
        {
            Document full = document;
            full.layer(middleId)->strokes.append(active);
            const QImage expected = RenderEngine::render(full, 2);

            const RenderEngine::LayerSplitFrame split =
                RenderEngine::renderLayerSplit(
                    document, 2, document.size, middleId);
            QVERIFY(split.valid);
            QImage layerImage = split.layerBase;
            QVERIFY(RenderEngine::renderStrokesOnLayer(
                layerImage, document, {active}, 2, document.size));
            const QImage actual =
                RenderEngine::composeLayerSplit(split, layerImage);

            QCOMPARE(actual.size(), expected.size());
            for (int y = 0; y < expected.height(); ++y)
            {
                for (int x = 0; x < expected.width(); ++x)
                {
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

    void composesActiveStrokeRegionsLikeFullPreviews()
    {
        Document document = Document::createDefault(QSize(256, 192));
        document.background = Qt::transparent;
        document.animationFrames = 8;
        document.wobbleAmount = 2.0;
        Layer &target = document.layers.first();
        target.strokes.append(makeStroke(StrokeMode::Paint,
            QColor(210, 55, 75),
            42.0,
            71,
            {QPointF(42.0, 96.0), QPointF(214.0, 96.0)}));
        const QUuid targetId = target.id;

        Layer top;
        top.name = QStringLiteral("Top");
        top.initialCanvasSize = document.size;
        top.opacity = 0.7;
        top.strokes.append(makeStroke(StrokeMode::Paint,
            QColor(45, 95, 220),
            18.0,
            72,
            {QPointF(128.0, 28.0), QPointF(128.0, 164.0)}));
        document.layers.append(top);

        QVector<Stroke> activeStrokes{makeStroke(StrokeMode::Paint,
                                          QColor(235, 195, 45, 180),
                                          14.0,
                                          73,
                                          {QPointF(78.0, 58.0),
                                              QPointF(116.0, 76.0),
                                              QPointF(156.0, 54.0)}),
            makeStroke(StrokeMode::Erase,
                Qt::black,
                24.0,
                74,
                {QPointF(82.0, 112.0), QPointF(172.0, 128.0)})};
        for (Stroke &stroke : activeStrokes)
        {
            stroke.clipMask =
                rectangularMask(document.size, QRect(56, 36, 144, 120));
        }

        for (const QSize outputSize : {document.size, QSize(128, 96)})
        {
            const RenderEngine::LayerSplitFrame split =
                RenderEngine::renderLayerSplit(
                    document, 3, outputSize, targetId);
            QVERIFY(split.valid);
            for (const Stroke &active : activeStrokes)
            {
                QImage fullLayer = split.layerBase;
                QVERIFY(RenderEngine::renderStrokesOnLayer(
                    fullLayer, document, {active}, 3, outputSize));
                const QImage full =
                    RenderEngine::composeLayerSplit(split, fullLayer);

                const QRect bounds = RenderEngine::strokePreviewBounds(
                    document, active, outputSize);
                QVERIFY(!bounds.isEmpty());
                QVERIFY(bounds.size() != outputSize);
                QImage regionalLayer = split.layerBase.copy(bounds);
                RenderEngine::StrokeRenderCache cache;
                QVERIFY(RenderEngine::renderStrokesOnLayerRegion(regionalLayer,
                    document,
                    {active},
                    3,
                    outputSize,
                    bounds,
                    &cache));
                const QImage regional = RenderEngine::composeLayerSplitRegion(
                    split, regionalLayer, bounds);

                QCOMPARE(regional, full.copy(bounds));
                QCOMPARE(cache.clipPaths.size(), 1);
                QImage reusedLayer = split.layerBase.copy(bounds);
                QVERIFY(RenderEngine::renderStrokesOnLayerRegion(reusedLayer,
                    document,
                    {active},
                    3,
                    outputSize,
                    bounds,
                    &cache));
                QCOMPARE(cache.clipPaths.size(), 1);
                QCOMPARE(RenderEngine::composeLayerSplitRegion(
                             split, reusedLayer, bounds),
                    regional);
            }
        }
    }

    void benchmarksFourKActiveStrokeRegions()
    {
        const QSize canvasSize(4096, 4096);
        Document document = Document::createDefault(canvasSize);
        document.background = Qt::transparent;
        document.wobbleAmount = 0.0;

        QVector<QPointF> positions;
        constexpr int pointCount = 3000;
        positions.reserve(pointCount);
        for (int index = 0; index < pointCount; ++index)
        {
            const qreal progress = static_cast<qreal>(index) / (pointCount - 1);
            positions.append(QPointF(256.0 + progress * 3584.0,
                2048.0 + std::sin(progress * 80.0) * 5.0));
        }
        const Stroke active = makeStroke(
            StrokeMode::Paint, QColor(45, 180, 105), 12.0, 82, positions);
        QImage layerBase(canvasSize, QImage::Format_ARGB32_Premultiplied);
        QVERIFY(!layerBase.isNull());
        layerBase.fill(Qt::transparent);
        const QRect bounds =
            RenderEngine::strokePreviewBounds(document, active, canvasSize);
        QVERIFY(!bounds.isEmpty());

        QElapsedTimer timer;
        timer.start();
        QImage fullLayer = layerBase;
        QVERIFY(RenderEngine::renderStrokesOnLayer(
            fullLayer, document, {active}, 0, canvasSize));
        const qint64 fullNanoseconds = timer.nsecsElapsed();
        const quint64 fullBytes = static_cast<quint64>(fullLayer.sizeInBytes());
        const QImage expectedRegion = fullLayer.copy(bounds);
        fullLayer = {};

        timer.restart();
        QImage regionalLayer = layerBase.copy(bounds);
        QVERIFY(RenderEngine::renderStrokesOnLayerRegion(
            regionalLayer, document, {active}, 0, canvasSize, bounds));
        const qint64 regionalNanoseconds = timer.nsecsElapsed();

        QCOMPARE(regionalLayer, expectedRegion);
        const quint64 regionalBytes =
            static_cast<quint64>(regionalLayer.sizeInBytes());
        QVERIFY(regionalBytes * 10 < fullBytes);
        qInfo().nospace() << "4K active-stroke layer replay: full "
                          << fullNanoseconds / 1000000.0 << " ms / "
                          << fullBytes << " bytes, region "
                          << regionalNanoseconds / 1000000.0 << " ms / "
                          << regionalBytes << " bytes";
    }

    void incrementallyRendersActiveStrokeTilesLikeFullReplay()
    {
        Document document = Document::createDefault(QSize(640, 480));
        document.background = Qt::transparent;
        document.animationFrames = 12;
        document.wobbleAmount = 2.5;
        QImage base(document.size, QImage::Format_ARGB32_Premultiplied);
        base.fill(QColor(180, 70, 55, 210));
        QPainter basePainter(&base);
        basePainter.setCompositionMode(QPainter::CompositionMode_Source);
        basePainter.fillRect(
            QRect(220, 120, 190, 230), QColor(40, 130, 210, 170));
        basePainter.end();

        QVector<Stroke> strokes;
        Stroke line;
        line.mode = StrokeMode::Paint;
        line.color = QColor(245, 215, 35, 190);
        line.width = 13.0;
        line.seed = 0x12345678ULL;
        line.brush.antialiasing = true;
        line.brush.opacity = 0.72;
        strokes.append(line);

        Stroke pressureLine = line;
        pressureLine.id = QUuid::createUuid();
        pressureLine.seed = 0x23456789ULL;
        pressureLine.brush.opacity = 1.0;
        strokes.append(pressureLine);

        Stroke airbrush = line;
        airbrush.id = QUuid::createUuid();
        airbrush.seed = 0x3456789aULL;
        airbrush.brush.engine = BrushEngine::Airbrush;
        airbrush.brush.spacing = 0.22;
        airbrush.brush.hardness = 0.35;
        airbrush.brush.flow = 0.25;
        strokes.append(airbrush);

        Stroke spray = line;
        spray.id = QUuid::createUuid();
        spray.seed = 0x456789abULL;
        spray.brush.engine = BrushEngine::Spray;
        spray.brush.spacing = 0.35;
        spray.brush.scatter = 1.4;
        spray.brush.particleSize = 0.16;
        spray.brush.sizeJitter = 0.6;
        spray.brush.density = 1.5;
        strokes.append(spray);

        Stroke eraser = airbrush;
        eraser.id = QUuid::createUuid();
        eraser.seed = 0x56789abcULL;
        eraser.mode = StrokeMode::Erase;
        eraser.width = 24.0;
        strokes.append(eraser);

        QVector<QPointF> positions;
        for (int index = 0; index < 180; ++index)
        {
            const qreal progress = static_cast<qreal>(index) / 179.0;
            positions.append(QPointF(18.0 + progress * 604.0,
                240.0 + std::sin(progress * 7.0 * std::numbers::pi) * 150.0));
        }

        for (int strokeIndex = 0; strokeIndex < strokes.size(); ++strokeIndex)
        {
            Stroke stroke = strokes[strokeIndex];
            IncrementalStrokeRenderer renderer;
            for (int index = 0; index < positions.size(); ++index)
            {
                const qreal pressure =
                    strokeIndex == 1 ? 0.2 + 0.8 * ((index % 17) / 16.0) : 0.75;
                stroke.points.append({positions[index], pressure});
                const IncrementalStrokeRenderer::Update update =
                    renderer.update(base, document, stroke, 5, document.size);
                QVERIFY(update.valid);
                QVERIFY(update.sourcePointsProcessed <= 2);

                QImage actual = base;
                QVERIFY(renderer.applyTo(actual));
                QImage expected = base;
                QVERIFY(RenderEngine::renderStrokesOnLayer(
                    expected, document, {stroke}, 5, document.size));
                QCOMPARE(actual, expected);
            }
        }
    }

    void boundsLongPrefixReplayWorkAtFourK()
    {
        const QSize canvasSize(4096, 4096);
        Document document = Document::createDefault(canvasSize);
        document.background = Qt::transparent;
        document.animationFrames = 12;
        document.wobbleAmount = 3.0;

        for (const int pointCount : {10000, 50000})
        {
            Stroke stroke;
            stroke.seed = 0x6f5e4d3cULL;
            stroke.width = 9.0;
            stroke.brush.antialiasing = false;
            stroke.points.reserve(pointCount);
            StrokeRenderer::IncrementalGeometry geometry;
            quint64 processedPoints = 0;
            quint64 rebuilds = 0;
            QElapsedTimer timer;
            timer.start();
            for (int index = 0; index < pointCount; ++index)
            {
                const int row = index / 256;
                const int column = index % 256;
                const qreal x =
                    row % 2 == 0 ? column * 16.0 : 4080.0 - column * 16.0;
                const qreal y = 8.0 + row * 20.0;
                stroke.points.append({QPointF(x, std::min(y, 4088.0)),
                    0.35 + (index % 13) * 0.05});
                const StrokeRenderer::GeometryUpdate update = geometry.update(
                    stroke, 4, document.animationFrames, document.wobbleAmount);
                QVERIFY(update.valid);
                processedPoints += update.sourcePointsProcessed;
                rebuilds += update.rebuilt ? 1 : 0;
            }
            const qint64 elapsed = timer.elapsed();
            const StrokeRenderer::PreparedStroke expected =
                StrokeRenderer::prepare(
                    stroke, 4, document.animationFrames, document.wobbleAmount);
            QCOMPARE(geometry.prepared().points, expected.points);
            QCOMPARE(geometry.prepared().width, expected.width);
            QCOMPARE(processedPoints, static_cast<quint64>(pointCount));
            QCOMPARE(rebuilds, 1ULL);
            qInfo().nospace()
                << "4K " << pointCount
                << "-point incremental geometry replay took " << elapsed
                << " ms and processed " << processedPoints << " source points";
        }

        QImage base(canvasSize, QImage::Format_ARGB32_Premultiplied);
        QVERIFY(!base.isNull());
        base.fill(Qt::transparent);
        const auto benchmarkRasterPrefix = [&](int pointCount)
        {
            Stroke stroke;
            stroke.seed = 0x7a6b5c4dULL;
            stroke.width = 9.0;
            stroke.brush.antialiasing = false;
            stroke.points.reserve(pointCount);
            IncrementalStrokeRenderer renderer;
            quint64 processedPoints = 0;
            quint64 renderedPixels = 0;
            quint64 cachedTileBytes = 0;
            QVector<qint64> updateNanoseconds;
            updateNanoseconds.reserve(pointCount);
            QElapsedTimer timer;
            timer.start();
            for (int index = 0; index < pointCount; ++index)
            {
                const int row = index / 256;
                const int column = index % 256;
                const qreal x =
                    row % 2 == 0 ? column * 16.0 : 4080.0 - column * 16.0;
                stroke.points.append(
                    {QPointF(x, 8.0 + row * 20.0), 0.4 + (index % 11) * 0.05});
                QElapsedTimer updateTimer;
                updateTimer.start();
                const IncrementalStrokeRenderer::Update update =
                    renderer.update(base, document, stroke, 4, canvasSize);
                updateNanoseconds.append(updateTimer.nsecsElapsed());
                QVERIFY(update.valid);
                processedPoints += update.sourcePointsProcessed;
                renderedPixels += update.pixelsRendered;
                cachedTileBytes = update.cachedTileBytes;
            }
            const qint64 elapsed = timer.elapsed();
            QImage actual = base;
            QVERIFY(renderer.applyTo(actual));
            QImage expected = base;
            QVERIFY(RenderEngine::renderStrokesOnLayer(
                expected, document, {stroke}, 4, canvasSize));
            QCOMPARE(actual, expected);
            QCOMPARE(processedPoints, static_cast<quint64>(pointCount));
            QVERIFY(renderedPixels <= static_cast<quint64>(pointCount) * 4ULL
                                          * 256ULL * 256ULL);
            QVERIFY(cachedTileBytes <= static_cast<quint64>(canvasSize.width())
                                           * canvasSize.height()
                                           * sizeof(QRgb));
            std::sort(updateNanoseconds.begin(), updateNanoseconds.end());
            const qreal p50Milliseconds =
                updateNanoseconds[updateNanoseconds.size() * 50 / 100]
                / 1000000.0;
            const qreal p95Milliseconds =
                updateNanoseconds[updateNanoseconds.size() * 95 / 100]
                / 1000000.0;
            qInfo().nospace()
                << "4K " << pointCount << "-point incremental tile replay took "
                << elapsed << " ms, processed " << renderedPixels
                << " pixels, and retained " << cachedTileBytes << " bytes; p50 "
                << p50Milliseconds << " ms, p95 " << p95Milliseconds << " ms";
        };

#ifdef NDEBUG
        constexpr std::array rasterPointCounts{10000, 50000};
#else
        constexpr std::array rasterPointCounts{1000, 5000};
#endif
        for (const int pointCount : rasterPointCounts)
        {
            benchmarkRasterPrefix(pointCount);
        }
    }

    void displacesWobbleLinearlyWithAmount()
    {
        QVector<QPointF> positions;
        for (int index = 0; index <= 20; ++index)
        {
            positions.append(QPointF(10.0 + index * 9.0, 60.0));
        }
        const Stroke stroke = makeStroke(StrokeMode::Paint,
            QColor(10, 20, 30),
            6.0,
            0xfeedbeefULL,
            positions);

        const QPainterPath still = RenderEngine::strokePath(stroke, 3, 12, 0.0);
        const QPainterPath single =
            RenderEngine::strokePath(stroke, 3, 12, 2.0);
        const QPainterPath doubled =
            RenderEngine::strokePath(stroke, 3, 12, 4.0);

        QCOMPARE(single.elementCount(), still.elementCount());
        QCOMPARE(doubled.elementCount(), still.elementCount());
        qreal largestOffset = 0.0;
        for (int index = 0; index < still.elementCount(); ++index)
        {
            const QPointF base(
                still.elementAt(index).x, still.elementAt(index).y);
            const QPointF offsetSingle =
                QPointF(single.elementAt(index).x, single.elementAt(index).y)
                - base;
            const QPointF offsetDouble =
                QPointF(doubled.elementAt(index).x, doubled.elementAt(index).y)
                - base;
            largestOffset = std::max(
                largestOffset, std::hypot(offsetSingle.x(), offsetSingle.y()));
            QVERIFY(std::abs(offsetDouble.x() - offsetSingle.x() * 2.0) < 1e-6);
            QVERIFY(std::abs(offsetDouble.y() - offsetSingle.y() * 2.0) < 1e-6);
        }
        QVERIFY(largestOffset > 0.05);
    }

    void rendersCrispPixelEdges()
    {
        Document document = Document::createDefault(QSize(64, 64));
        document.wobbleAmount = 0.0;
        const QColor strokeColor(255, 120, 120);
        document.layers.first().strokes.append(makeStroke(StrokeMode::Paint,
            strokeColor,
            3.0,
            45,
            {QPointF(7.25, 51.75),
                QPointF(22.5, 11.25),
                QPointF(55.75, 44.5)}));

        const QImage image = RenderEngine::render(document, 0);
        QVERIFY(!image.isNull());
        int paintedPixels = 0;
        for (int y = 0; y < image.height(); ++y)
        {
            for (int x = 0; x < image.width(); ++x)
            {
                const QColor pixel = image.pixelColor(x, y);
                QVERIFY(pixel == document.background || pixel == strokeColor);
                if (pixel == strokeColor)
                {
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
        Stroke stroke = makeStroke(StrokeMode::Paint,
            strokeColor,
            3.0,
            45,
            {QPointF(7.25, 51.75), QPointF(22.5, 11.25), QPointF(55.75, 44.5)});
        stroke.brush.antialiasing = true;
        document.layers.first().strokes.append(stroke);

        const QImage image = RenderEngine::render(document, 0);
        QVERIFY(!image.isNull());
        int blendedPixels = 0;
        for (int y = 0; y < image.height(); ++y)
        {
            for (int x = 0; x < image.width(); ++x)
            {
                const QColor pixel = image.pixelColor(x, y);
                if (pixel != document.background && pixel != strokeColor)
                {
                    ++blendedPixels;
                }
            }
        }
        QVERIFY(blendedPixels > 0);
    }

    void rejectsUnallocatableCanvas()
    {
        Document document = Document::createDefault(QSize(
            std::numeric_limits<int>::max(), std::numeric_limits<int>::max()));
        QVERIFY(RenderEngine::render(document, 0).isNull());
    }

    void ignoresUnsafeStrokeCoordinates()
    {
        Document document = Document::createDefault(QSize(64, 64));
        Stroke stroke;
        stroke.points = {
            {QPointF(std::numeric_limits<qreal>::infinity(), 16.0), 1.0}};
        document.layers.first().strokes.append(stroke);

        const QImage image = RenderEngine::render(document, 0);
        QVERIFY(!image.isNull());
        QCOMPARE(image.pixelColor(16, 16), QColor(Qt::white));
        QVERIFY(RenderEngine::strokePath(
            stroke, 0, document.animationFrames, document.wobbleAmount)
                .isEmpty());
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
        bottom.strokes.append(makeStroke(StrokeMode::Paint,
            QColor(20, 180, 80),
            20.0,
            10,
            {QPointF(8.0, 32.0), QPointF(72.0, 32.0)}));

        Layer middle;
        middle.name = QStringLiteral("Middle");
        middle.strokes.append(makeStroke(StrokeMode::Paint,
            QColor(220, 40, 50),
            20.0,
            20,
            {QPointF(8.0, 32.0), QPointF(72.0, 32.0)}));
        middle.strokes.append(makeStroke(
            StrokeMode::Erase, Qt::black, 14.0, 30, {QPointF(48.0, 32.0)}));

        Layer top;
        top.name = QStringLiteral("Top");
        top.strokes.append(makeStroke(
            StrokeMode::Erase, Qt::black, 14.0, 40, {QPointF(20.0, 32.0)}));

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
        document.layers.first().strokes.append(makeStroke(StrokeMode::Paint,
            Qt::black,
            4.0,
            50,
            {QPointF(8.0, 52.0), QPointF(32.0, 8.0), QPointF(56.0, 52.0)}));

        const QImage image = RenderEngine::render(document, 0);
        QCOMPARE(image.pixelColor(32, 46), QColor(Qt::white));
        QCOMPARE(image.pixelColor(32, 8), QColor(Qt::black));
    }

    void clipsPaintAndFillToSelectionMasks()
    {
        QImage clipMask(QSize(64, 64), QImage::Format_Grayscale8);
        clipMask.fill(0);
        for (int y = 12; y < 52; ++y)
        {
            std::fill_n(clipMask.scanLine(y) + 12, 20, 255);
        }

        Document paintDocument = Document::createDefault(QSize(64, 64));
        paintDocument.wobbleAmount = 0.0;
        Stroke paint = makeStroke(StrokeMode::Paint,
            QColor(220, 30, 40),
            16.0,
            60,
            {QPointF(4.0, 32.0), QPointF(60.0, 32.0)});
        paint.clipMask = clipMask;
        paintDocument.layers.first().strokes.append(paint);
        const QImage painted = RenderEngine::render(paintDocument, 0);
        QCOMPARE(painted.pixelColor(20, 32), paint.color);
        QCOMPARE(painted.pixelColor(45, 32), QColor(Qt::white));

        Document fillDocument = Document::createDefault(QSize(64, 64));
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

    void treatsOnlyFillMaskSamplesAtLeastHalfOpaqueAsIncluded()
    {
        Document document = Document::createDefault(QSize(16, 16));
        document.background = Qt::transparent;
        Stroke fill;
        fill.mode = StrokeMode::Fill;
        fill.color = QColor(30, 80, 220);
        fill.points = {{QPointF(8.0, 8.0), 1.0}};
        fill.fillMask = QImage(document.size, QImage::Format_Grayscale8);
        fill.fillMask.fill(0);
        fill.fillMask.scanLine(3)[3] = 127;
        fill.fillMask.scanLine(11)[11] = 128;
        document.layers.first().strokes.append(fill);

        const QImage rendered = RenderEngine::render(document, 0);
        QCOMPARE(rendered.pixelColor(3, 3), QColor(Qt::transparent));
        QCOMPARE(rendered.pixelColor(11, 11), fill.color);
    }

    void canvasCropPreservesDisconnectedFillPixels()
    {
        Document document = Document::createDefault(QSize(10, 10));
        document.wobbleAmount = 0.0;

        Stroke barrier = makeStroke(StrokeMode::Paint,
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

        const QImage cropped = RenderEngine::render(controller.document(), 0);
        const QImage expected = before.copy(QRect(0, 0, 10, 8));
        QCOMPARE(cropped, expected);
    }

    void movesFlattenedPaintEraseSelectionOverExistingPixels()
    {
        Document document = Document::createDefault(QSize(96, 48));
        document.background = Qt::transparent;
        document.animationFrames = 4;
        document.wobbleAmount = 5.0;
        Stroke sourcePaint = makeStroke(StrokeMode::Paint,
            QColor(30, 90, 220),
            18.0,
            71,
            {QPointF(14.0, 24.0), QPointF(26.0, 24.0)});
        Stroke sourceErase = makeStroke(
            StrokeMode::Erase, Qt::black, 8.0, 72, {QPointF(20.0, 24.0)});
        Stroke destinationPaint = makeStroke(StrokeMode::Paint,
            QColor(220, 50, 40),
            18.0,
            73,
            {QPointF(54.0, 24.0), QPointF(66.0, 24.0)});
        sourcePaint.brush.antialiasing = false;
        sourceErase.brush.antialiasing = false;
        destinationPaint.brush.antialiasing = false;
        document.layers.first().strokes = {
            sourcePaint, sourceErase, destinationPaint};

        const QImage selection =
            rectangularMask(document.size, QRect(4, 10, 32, 29));
        const QPoint delta(40, 0);
        QVector<QImage> beforeFrames;
        QVector<QImage> expectedFrames;
        for (int frame = 0; frame < document.animationFrames; ++frame)
        {
            const QImage before = activeLayerPixels(document, frame);
            QVERIFY(!before.isNull());
            beforeFrames.append(before);
            expectedFrames.append(
                rasterSelectionResult(before, selection, delta, true));
            QVERIFY(!expectedFrames.last().isNull());
        }
        QVERIFY(beforeFrames[0] != beforeFrames[1]);

        DocumentController controller;
        controller.loadDocument(document);
        QVERIFY(controller.moveStrokes(document.activeLayerId,
            {sourcePaint.id, sourceErase.id},
            delta,
            selection));
        for (int frame = 0; frame < document.animationFrames; ++frame)
        {
            QCOMPARE(activeLayerPixels(controller.document(), frame),
                expectedFrames[frame]);
        }

        controller.undoStack()->undo();
        for (int frame = 0; frame < document.animationFrames; ++frame)
        {
            QCOMPARE(activeLayerPixels(controller.document(), frame),
                beforeFrames[frame]);
        }
        controller.undoStack()->redo();
        for (int frame = 0; frame < document.animationFrames; ++frame)
        {
            QCOMPARE(activeLayerPixels(controller.document(), frame),
                expectedFrames[frame]);
        }
    }

    void duplicatesFlattenedPaintEraseSelectionOverExistingPixels()
    {
        Document document = Document::createDefault(QSize(96, 48));
        document.background = Qt::transparent;
        document.wobbleAmount = 0.0;
        Stroke sourcePaint = makeStroke(StrokeMode::Paint,
            QColor(35, 170, 90),
            18.0,
            81,
            {QPointF(14.0, 24.0), QPointF(26.0, 24.0)});
        Stroke sourceErase = makeStroke(
            StrokeMode::Erase, Qt::black, 8.0, 82, {QPointF(20.0, 24.0)});
        Stroke destinationPaint = makeStroke(StrokeMode::Paint,
            QColor(230, 170, 35),
            18.0,
            83,
            {QPointF(54.0, 24.0), QPointF(66.0, 24.0)});
        sourcePaint.brush.antialiasing = false;
        sourceErase.brush.antialiasing = false;
        destinationPaint.brush.antialiasing = false;
        document.layers.first().strokes = {
            sourcePaint, sourceErase, destinationPaint};

        const QImage selection =
            rectangularMask(document.size, QRect(4, 10, 32, 29));
        const QPoint delta(40, 0);
        const QImage before = activeLayerPixels(document);
        const QImage expected =
            rasterSelectionResult(before, selection, delta, false);
        QVERIFY(!before.isNull());
        QVERIFY(!expected.isNull());

        DocumentController controller;
        controller.loadDocument(document);
        QVERIFY(controller.duplicateStrokes(document.activeLayerId,
            {sourcePaint.id, sourceErase.id},
            delta,
            selection));
        QCOMPARE(activeLayerPixels(controller.document()), expected);
        QCOMPARE(activeLayerPixels(controller.document()).pixelColor(60, 24),
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
        Stroke sourcePaint = makeStroke(StrokeMode::Paint,
            QColor(40, 130, 225),
            18.0,
            86,
            {QPointF(14.0, 24.0), QPointF(26.0, 24.0)});
        Stroke sourceErase = makeStroke(
            StrokeMode::Erase, Qt::black, 8.0, 87, {QPointF(20.0, 24.0)});
        Stroke outsidePaint = makeStroke(StrokeMode::Paint,
            QColor(230, 80, 120),
            18.0,
            88,
            {QPointF(54.0, 24.0), QPointF(66.0, 24.0)});
        sourcePaint.brush.antialiasing = false;
        sourceErase.brush.antialiasing = false;
        outsidePaint.brush.antialiasing = false;
        document.layers.first().strokes = {
            sourcePaint, sourceErase, outsidePaint};

        const QImage selection =
            rectangularMask(document.size, QRect(4, 10, 32, 29));
        const QImage before = activeLayerPixels(document);
        const QImage expected = clearedSelectionResult(before, selection);
        QVERIFY(!before.isNull());
        QVERIFY(!expected.isNull());

        DocumentController controller;
        controller.loadDocument(document);
        QVERIFY(controller.removeSelectedContent(document.activeLayerId,
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
        sourceFill.fillMask =
            rectangularMask(document.size, QRect(8, 12, 24, 25));
        Stroke destinationPaint = makeStroke(StrokeMode::Paint,
            QColor(30, 180, 160),
            20.0,
            91,
            {QPointF(52.0, 24.0), QPointF(68.0, 24.0)});
        destinationPaint.brush.antialiasing = false;
        document.layers.first().strokes = {sourceFill, destinationPaint};

        const QImage selection =
            rectangularMask(document.size, QRect(8, 12, 24, 25));
        const QPoint delta(40, 0);
        const QImage before = activeLayerPixels(document);
        const QImage expected =
            rasterSelectionResult(before, selection, delta, true);
        QVERIFY(!before.isNull());
        QVERIFY(!expected.isNull());

        DocumentController controller;
        controller.loadDocument(document);
        QVERIFY(controller.moveStrokes(
            document.activeLayerId, {sourceFill.id}, delta, selection));
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
        Stroke paint = makeStroke(StrokeMode::Paint,
            QColor(35, 110, 225),
            20.0,
            101,
            {QPointF(10.0, 24.0), QPointF(44.0, 24.0)});
        Stroke erase = makeStroke(StrokeMode::Erase,
            Qt::black,
            8.0,
            102,
            {QPointF(20.0, 24.0), QPointF(25.0, 24.0)});
        paint.brush.antialiasing = false;
        erase.brush.antialiasing = false;
        document.layers.first().strokes = {paint, erase};

        const QImage selection =
            rectangularMask(document.size, QRect(2, 10, 46, 29));
        const QPoint delta(14, 0);
        const QImage before = activeLayerPixels(document);
        const QImage expected =
            rasterSelectionResult(before, selection, delta, true);
        QVERIFY(!before.isNull());
        QVERIFY(!expected.isNull());

        DocumentController controller;
        controller.loadDocument(document);
        QVERIFY(controller.moveStrokes(
            document.activeLayerId, {paint.id, erase.id}, delta, selection));

        const QImage actual = activeLayerPixels(controller.document());
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
        Stroke sourcePaint = makeStroke(StrokeMode::Paint,
            QColor(40, 110, 225),
            20.0,
            111,
            {QPointF(12.0, 30.0), QPointF(34.0, 25.0)});
        Stroke sourceErase = makeStroke(
            StrokeMode::Erase, Qt::black, 7.0, 112, {QPointF(22.0, 28.0)});
        Stroke existingDestination = makeStroke(StrokeMode::Paint,
            QColor(225, 70, 55),
            22.0,
            113,
            {QPointF(64.0, 30.0), QPointF(88.0, 30.0)});
        Stroke laterPaint = makeStroke(StrokeMode::Paint,
            QColor(250, 205, 45),
            9.0,
            114,
            {QPointF(58.0, 22.0), QPointF(94.0, 38.0)});
        Stroke laterErase = makeStroke(StrokeMode::Erase,
            Qt::black,
            5.0,
            115,
            {QPointF(78.0, 20.0), QPointF(78.0, 42.0)});
        for (Stroke *stroke : {&sourcePaint,
                 &sourceErase,
                 &existingDestination,
                 &laterPaint,
                 &laterErase})
        {
            stroke->brush.antialiasing = false;
        }
        document.layers.first().strokes = {
            sourcePaint, sourceErase, existingDestination};

        const QImage selection =
            rectangularMask(document.size, QRect(2, 10, 42, 41));
        const QPoint delta(52, 0);
        QVector<QImage> beforeFrames;
        QVector<QImage> movedFrames;
        QVector<QImage> expectedFrames;
        beforeFrames.reserve(document.animationFrames);
        movedFrames.reserve(document.animationFrames);
        expectedFrames.reserve(document.animationFrames);
        for (int frame = 0; frame < document.animationFrames; ++frame)
        {
            const QImage before = activeLayerPixels(document, frame);
            const QImage moved =
                rasterSelectionResult(before, selection, delta, true);
            const QImage expected = renderAdditionalStrokes(
                moved, document, {laterPaint, laterErase}, frame);
            QVERIFY(!before.isNull());
            QVERIFY(!moved.isNull());
            QVERIFY(!expected.isNull());
            beforeFrames.append(before);
            movedFrames.append(moved);
            expectedFrames.append(expected);
        }

        DocumentController controller;
        controller.loadDocument(document);
        QVERIFY(controller.moveStrokes(document.activeLayerId,
            {sourcePaint.id, sourceErase.id},
            delta,
            selection));
        controller.addStroke(document.activeLayerId, laterPaint);
        controller.addStroke(document.activeLayerId, laterErase);

        for (int frame = 0; frame < document.animationFrames; ++frame)
        {
            QCOMPARE(activeLayerPixels(controller.document(), frame),
                expectedFrames[frame]);
        }

        controller.undoStack()->undo();
        controller.undoStack()->undo();
        for (int frame = 0; frame < document.animationFrames; ++frame)
        {
            QCOMPARE(activeLayerPixels(controller.document(), frame),
                movedFrames[frame]);
        }
        controller.undoStack()->undo();
        for (int frame = 0; frame < document.animationFrames; ++frame)
        {
            QCOMPARE(activeLayerPixels(controller.document(), frame),
                beforeFrames[frame]);
        }
    }

    void appliesSequentialSelectionTransformsAsSeparateRasterOperations()
    {
        Document document = Document::createDefault(QSize(112, 56));
        document.background = Qt::transparent;
        document.animationFrames = 5;
        document.wobbleAmount = 3.0;
        Stroke blue = makeStroke(StrokeMode::Paint,
            QColor(35, 95, 220),
            15.0,
            121,
            {QPointF(10.0, 18.0), QPointF(30.0, 18.0)});
        Stroke green = makeStroke(StrokeMode::Paint,
            QColor(45, 190, 105),
            9.0,
            122,
            {QPointF(12.0, 34.0), QPointF(22.0, 27.0)});
        Stroke erase = makeStroke(
            StrokeMode::Erase, Qt::black, 5.0, 123, {QPointF(16.0, 18.0)});
        Stroke destination = makeStroke(StrokeMode::Paint,
            QColor(225, 65, 75),
            25.0,
            124,
            {QPointF(60.0, 28.0), QPointF(90.0, 28.0)});
        for (Stroke *stroke : {&blue, &green, &erase, &destination})
        {
            stroke->brush.antialiasing = false;
        }
        document.layers.first().strokes = {blue, green, erase, destination};

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
        for (int frame = 0; frame < document.animationFrames; ++frame)
        {
            const QImage before = activeLayerPixels(document, frame);
            const QImage afterMove = rasterSelectionTransformResult(
                before, firstSelection, moveTransform, true);
            const QImage afterFlip = rasterSelectionTransformResult(
                afterMove, secondSelection, flipTransform, true);
            QVERIFY(!before.isNull());
            QVERIFY(!afterMove.isNull());
            QVERIFY(!afterFlip.isNull());
            beforeFrames.append(before);
            afterMoveFrames.append(afterMove);
            afterFlipFrames.append(afterFlip);
        }

        const QVector<QUuid> sourceIds = {blue.id, green.id, erase.id};
        DocumentController controller;
        controller.loadDocument(document);
        QVERIFY(controller.moveStrokes(
            document.activeLayerId, sourceIds, delta, firstSelection));
        QVERIFY(controller.flipStrokes(document.activeLayerId,
            sourceIds,
            flipCenter,
            true,
            secondSelection));
        for (int frame = 0; frame < document.animationFrames; ++frame)
        {
            QCOMPARE(activeLayerPixels(controller.document(), frame),
                afterFlipFrames[frame]);
        }

        controller.undoStack()->undo();
        for (int frame = 0; frame < document.animationFrames; ++frame)
        {
            QCOMPARE(activeLayerPixels(controller.document(), frame),
                afterMoveFrames[frame]);
        }
        controller.undoStack()->undo();
        for (int frame = 0; frame < document.animationFrames; ++frame)
        {
            QCOMPARE(activeLayerPixels(controller.document(), frame),
                beforeFrames[frame]);
        }
        controller.undoStack()->redo();
        controller.undoStack()->redo();
        for (int frame = 0; frame < document.animationFrames; ++frame)
        {
            QCOMPARE(activeLayerPixels(controller.document(), frame),
                afterFlipFrames[frame]);
        }
    }

    void replaysSelectionTransformForEveryConfiguredAnimationFrame()
    {
        Document document = Document::createDefault(QSize(88, 48));
        document.background = Qt::transparent;
        document.animationFrames = DocumentLimits::maximumAnimationFrames;
        document.wobbleAmount = 8.0;
        Stroke animated = makeStroke(StrokeMode::Paint,
            QColor(85, 55, 220),
            13.0,
            131,
            {QPointF(7.0, 13.0), QPointF(18.0, 35.0), QPointF(32.0, 17.0)});
        animated.brush.antialiasing = false;
        animated.brush.animatedJitter = true;
        document.layers.first().strokes = {animated};

        const QImage selection =
            rectangularMask(document.size, QRect(0, 4, 40, 40));
        const QPoint delta(42, 0);
        QVector<QImage> expectedFrames;
        expectedFrames.reserve(document.animationFrames);
        bool sawAnimation = false;
        QImage firstBefore;
        for (int frame = 0; frame < document.animationFrames; ++frame)
        {
            const QImage before = activeLayerPixels(document, frame);
            if (frame == 0)
            {
                firstBefore = before;
            }
            else if (before != firstBefore)
            {
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
            document.activeLayerId, {animated.id}, delta, selection));
        QCOMPARE(controller.document().animationFrames,
            DocumentLimits::maximumAnimationFrames);
        for (int frame = 0; frame < document.animationFrames; ++frame)
        {
            QCOMPARE(activeLayerPixels(controller.document(), frame),
                expectedFrames[frame]);
        }
    }

    void nonUniformImageResizeScalesCommittedLayerPixelsExactly()
    {
        Document document = Document::createDefault(QSize(73, 47));
        document.background = Qt::transparent;
        document.animationFrames = 6;
        document.wobbleAmount = 4.0;
        Stroke paint = makeStroke(StrokeMode::Paint,
            QColor(30, 135, 225),
            11.0,
            141,
            {QPointF(6.0, 8.0), QPointF(31.0, 38.0), QPointF(61.0, 13.0)});
        Stroke erase = makeStroke(StrokeMode::Erase,
            Qt::black,
            5.0,
            142,
            {QPointF(28.0, 29.0), QPointF(42.0, 23.0)});
        Stroke fill;
        fill.mode = StrokeMode::Fill;
        fill.color = QColor(235, 180, 40);
        fill.points = {{QPointF(4.0, 4.0), 1.0}};
        fill.fillMask = rectangularMask(document.size, QRect(2, 2, 12, 9));
        paint.brush.antialiasing = false;
        erase.brush.antialiasing = false;
        document.layers.first().strokes = {paint, erase, fill};

        const QSize targetSize(119, 61);
        QVector<QImage> beforeFrames;
        QVector<QImage> expectedFrames;
        for (int frame = 0; frame < document.animationFrames; ++frame)
        {
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
        for (int frame = 0; frame < document.animationFrames; ++frame)
        {
            QCOMPARE(activeLayerPixels(controller.document(), frame),
                expectedFrames[frame]);
        }

        controller.undoStack()->undo();
        QCOMPARE(controller.document().size, document.size);
        for (int frame = 0; frame < document.animationFrames; ++frame)
        {
            QCOMPARE(activeLayerPixels(controller.document(), frame),
                beforeFrames[frame]);
        }
        controller.undoStack()->redo();
        QCOMPARE(controller.document().size, targetSize);
        for (int frame = 0; frame < document.animationFrames; ++frame)
        {
            QCOMPARE(activeLayerPixels(controller.document(), frame),
                expectedFrames[frame]);
        }
    }

    void canvasCropThenExpandKeepsPreexistingSelectionEditClipped()
    {
        Document document = Document::createDefault(QSize(96, 48));
        document.background = Qt::transparent;
        document.animationFrames = 4;
        document.wobbleAmount = 3.0;
        Stroke paint = makeStroke(StrokeMode::Paint,
            QColor(50, 120, 230),
            17.0,
            151,
            {QPointF(8.0, 24.0), QPointF(30.0, 24.0)});
        Stroke erase = makeStroke(
            StrokeMode::Erase, Qt::black, 5.0, 152, {QPointF(17.0, 24.0)});
        paint.brush.antialiasing = false;
        erase.brush.antialiasing = false;
        document.layers.first().strokes = {paint, erase};

        const QImage selection =
            rectangularMask(document.size, QRect(0, 10, 40, 29));
        const QPoint moveDelta(48, 0);
        const QSize croppedSize(68, 48);
        const QPoint cropOffset(0, 0);
        const QSize expandedSize(96, 48);
        const QPoint expandOffset(0, 0);
        QVector<QImage> movedFrames;
        QVector<QImage> croppedFrames;
        QVector<QImage> expandedFrames;
        for (int frame = 0; frame < document.animationFrames; ++frame)
        {
            const QImage before = activeLayerPixels(document, frame);
            const QImage moved =
                rasterSelectionResult(before, selection, moveDelta, true);
            const QImage cropped =
                reframedRasterResult(moved, croppedSize, cropOffset);
            const QImage expanded =
                reframedRasterResult(cropped, expandedSize, expandOffset);
            QVERIFY(!moved.isNull());
            QVERIFY(!cropped.isNull());
            QVERIFY(!expanded.isNull());
            movedFrames.append(moved);
            croppedFrames.append(cropped);
            expandedFrames.append(expanded);
        }

        DocumentController controller;
        controller.loadDocument(document);
        QVERIFY(controller.moveStrokes(document.activeLayerId,
            {paint.id, erase.id},
            moveDelta,
            selection));
        for (int frame = 0; frame < document.animationFrames; ++frame)
        {
            QCOMPARE(activeLayerPixels(controller.document(), frame),
                movedFrames[frame]);
        }

        QVERIFY(controller.resizeCanvas(croppedSize, cropOffset));
        for (int frame = 0; frame < document.animationFrames; ++frame)
        {
            QCOMPARE(activeLayerPixels(controller.document(), frame),
                croppedFrames[frame]);
        }

        QVERIFY(controller.resizeCanvas(expandedSize, expandOffset));
        for (int frame = 0; frame < document.animationFrames; ++frame)
        {
            const QImage actual =
                activeLayerPixels(controller.document(), frame);
            QCOMPARE(actual, expandedFrames[frame]);
            QCOMPARE(actual.pixelColor(80, 24), QColor(Qt::transparent));
        }

        controller.undoStack()->undo();
        for (int frame = 0; frame < document.animationFrames; ++frame)
        {
            QCOMPARE(activeLayerPixels(controller.document(), frame),
                croppedFrames[frame]);
        }
        controller.undoStack()->undo();
        for (int frame = 0; frame < document.animationFrames; ++frame)
        {
            QCOMPARE(activeLayerPixels(controller.document(), frame),
                movedFrames[frame]);
        }
    }

    void full4kSelectionUsesPackedStorageAndBoundedFrameRendering()
    {
        constexpr int edge = DocumentLimits::maximumCanvasEdge;
        const QSize canvasSize(edge, edge);
        Document document = Document::createDefault(canvasSize);
        document.background = Qt::transparent;
        document.animationFrames = DocumentLimits::maximumAnimationFrames;
        document.wobbleAmount = 5.0;
        Stroke paint = makeStroke(StrokeMode::Paint,
            QColor(45, 105, 225),
            5.0,
            161,
            {QPointF(16.0, edge / 2.0), QPointF(edge - 16.0, edge / 2.0)});
        paint.brush.antialiasing = false;
        document.layers.first().strokes = {paint};

        QImage selection(canvasSize, QImage::Format_Grayscale8);
        QVERIFY(!selection.isNull());
        quint32 random = 0x6d2b79f5U;
        for (int y = 0; y < edge; ++y)
        {
            uchar *line = selection.scanLine(y);
            for (int x = 0; x < edge; ++x)
            {
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
            document.activeLayerId, {paint.id}, QPointF(0.0, 1.0), selection));

        const Layer &layer = controller.document().layers.first();
        QVERIFY(!layer.strokes.isEmpty());
        const Stroke &operation = layer.strokes.last();
        QCOMPARE(operation.mode, StrokeMode::PixelSelection);
        QVERIFY(operation.pixelSelectionOp.has_value());
        const PixelSelectionOp &pixelOperation = *operation.pixelSelectionOp;
        QCOMPARE(pixelOperation.canvasSize, canvasSize);
        QCOMPARE(pixelOperation.sourceBounds, QRect(QPoint(), canvasSize));
        const qsizetype expectedStride = (static_cast<qsizetype>(edge) + 7) / 8;
        const qsizetype expectedPackedBytes = expectedStride * edge;
        QCOMPARE(expectedPackedBytes, qsizetype(2 * 1024 * 1024));
        QCOMPARE(pixelOperation.packedMask.size(), expectedPackedBytes);
        QCOMPARE(packedSelectionBytes(controller.document()),
            quint64(expectedPackedBytes));
        QVERIFY(packedSelectionBytes(controller.document())
                <= DocumentLimits::maximumDistinctClipMaskBytes);

        selection = {};
        const quint64 expectedFrameBytes = static_cast<quint64>(edge)
                                           * static_cast<quint64>(edge)
                                           * sizeof(QRgb);
        QCOMPARE(expectedFrameBytes, quint64(64) * 1024ULL * 1024ULL);
        QImage rendered = activeLayerPixels(controller.document(), 0);
        QVERIFY(!rendered.isNull());
        QCOMPARE(rendered.size(), canvasSize);
        QCOMPARE(
            static_cast<quint64>(rendered.sizeInBytes()), expectedFrameBytes);
        rendered = {};
        rendered = activeLayerPixels(
            controller.document(), DocumentLimits::maximumAnimationFrames - 1);
        QVERIFY(!rendered.isNull());
        QCOMPARE(rendered.size(), canvasSize);
        QCOMPARE(
            static_cast<quint64>(rendered.sizeInBytes()), expectedFrameBytes);
    }

    void usesTabletPressureForWidth()
    {
        auto renderPressure = [](qreal pressure)
        {
            Document document = Document::createDefault(QSize(80, 64));
            document.wobbleAmount = 0.0;
            Stroke stroke;
            stroke.color = Qt::black;
            stroke.width = 20.0;
            stroke.points = {{QPointF(10.0, 32.0), pressure},
                {QPointF(70.0, 32.0), pressure}};
            document.layers.first().strokes.append(stroke);
            return RenderEngine::render(document, 0);
        };

        const QImage light = renderPressure(0.1);
        const QImage heavy = renderPressure(1.0);
        int lightPixels = 0;
        int heavyPixels = 0;
        for (int y = 0; y < light.height(); ++y)
        {
            for (int x = 0; x < light.width(); ++x)
            {
                if (light.pixelColor(x, y) != QColor(Qt::white))
                {
                    ++lightPixels;
                }
                if (heavy.pixelColor(x, y) != QColor(Qt::white))
                {
                    ++heavyPixels;
                }
            }
        }
        QVERIFY(heavyPixels > lightPixels * 2);
    }

    void rendersEveryBuiltInBrushDeterministically()
    {
        QSet<QString> ids;
        for (const BrushPreset &preset : BrushPresetCatalog::builtIns())
        {
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
                {QPointF(24.0, 48.0), 0.45}, {QPointF(104.0, 48.0), 1.0}};
            document.layers.first().strokes.append(stroke);

            const QImage first = RenderEngine::render(document, 3);
            const QImage second = RenderEngine::render(document, 3);
            QVERIFY2(!first.isNull(), qPrintable(preset.id));
            QVERIFY2(first == second, qPrintable(preset.id));
            QVERIFY2(std::any_of(first.constBits(),
                         first.constBits() + first.sizeInBytes(),
                         [](uchar value)
                         {
                             return value != 255;
                         }),
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
        stroke.points = {{QPointF(40.0, 40.0), 1.0}};
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
        auto renderPreset = [](const QString &presetId, int frame)
        {
            Document document = Document::createDefault(QSize(96, 72));
            document.wobbleAmount = 0.0;
            Stroke stroke;
            stroke.seed = 91;
            stroke.color = Qt::black;
            stroke.width = 44.0;
            stroke.brush = BrushPresetCatalog::find(presetId)->settings;
            stroke.points = {
                {QPointF(18.0, 36.0), 1.0}, {QPointF(78.0, 36.0), 1.0}};
            document.layers.first().strokes.append(stroke);
            return RenderEngine::render(document, frame);
        };

        QCOMPARE(renderPreset(QStringLiteral("pixel-spray"), 0),
            renderPreset(QStringLiteral("pixel-spray"), 1));
        QVERIFY(renderPreset(QStringLiteral("wobble-spray"), 0)
                != renderPreset(QStringLiteral("wobble-spray"), 1));
    }

    void handlesDotsAndDuplicatePoints()
    {
        Document document = Document::createDefault(QSize(64, 64));
        document.wobbleAmount = 5.0;

        const Stroke dot = makeStroke(
            StrokeMode::Paint, Qt::black, 8.0, 100, {QPointF(16.0, 16.0)});
        const Stroke duplicates = makeStroke(StrokeMode::Paint,
            QColor(200, 20, 40),
            8.0,
            200,
            {QPointF(40.0, 40.0), QPointF(40.0, 40.0), QPointF(40.0, 40.0)});
        document.layers.first().strokes = {dot, duplicates};

        const QPainterPath dotPath =
            RenderEngine::strokePath(dot, 3, document.animationFrames, 5.0);
        const QPainterPath duplicatePath = RenderEngine::strokePath(
            duplicates, 3, document.animationFrames, 5.0);
        QVERIFY(dotPath.elementCount() > 0);
        QVERIFY(duplicatePath.elementCount() > 0);
        const QPainterPath::Element dotElement = dotPath.elementAt(0);
        const QPainterPath::Element duplicateElement =
            duplicatePath.elementAt(0);
        QVERIFY(std::isfinite(dotElement.x));
        QVERIFY(std::isfinite(dotElement.y));
        QVERIFY(std::isfinite(duplicateElement.x));
        QVERIFY(std::isfinite(duplicateElement.y));

        const QImage image = RenderEngine::render(document, 3);
        QVERIFY(!image.isNull());
        QCOMPARE(image.size(), document.size);
        bool containsPaint = false;
        for (int y = 0; y < image.height() && !containsPaint; ++y)
        {
            for (int x = 0; x < image.width(); ++x)
            {
                if (image.pixelColor(x, y) != document.background)
                {
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
