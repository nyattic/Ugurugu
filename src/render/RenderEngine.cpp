#include "render/RenderEngine.hpp"

#include "document/DocumentLimits.hpp"
#include "document/DocumentOperations.hpp"
#include "document/SelectionOperation.hpp"
#include "document/StrokeMask.hpp"
#include "render/FloodFillMask.hpp"
#include "render/ImageAffineTransformer.hpp"
#include "render/ImageResampler.hpp"
#include "render/LayerCompositionPlan.hpp"
#include "render/StrokeCoverageRenderer.hpp"
#include "render/StrokeRenderer.hpp"
#include "render/engine/DisplayScaleReplay.hpp"
#include "render/engine/LayerHierarchyCompositor.hpp"
#include "render/engine/LayerOperationReplay.hpp"
#include "render/engine/PreviewScale.hpp"

#include <QHash>
#include <QPainter>
#include <QRadialGradient>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace wobble
{

using namespace render_detail;

QImage RenderEngine::fillRegionMask(const QImage &image, const QPoint &seed)
{
    return FloodFillMask::fromImage(
        image, seed, FloodFillMask::Comparison::AlphaBoundary, 0);
}

QImage RenderEngine::render(const Document &document, int frameIndex)
{
    return renderAtSize(document, frameIndex, document.size);
}

QImage RenderEngine::renderScaled(const Document &document,
    int frameIndex,
    const QSize &outputSize,
    ScaledRenderMode mode,
    ScaledRenderStats *stats)
{
    if (stats)
    {
        *stats = {};
    }
    if (mode == ScaledRenderMode::DisplayPreview
        && canReplayAtDisplayScale(document, outputSize))
    {
        if (stats)
        {
            stats->usedDisplayScaleReplay = true;
        }
        return renderAtDisplayScale(document, frameIndex, outputSize, stats);
    }
    if (stats)
    {
        stats->usedNativeExactFallback = true;
    }
    return renderAtSize(document, frameIndex, outputSize);
}

LayerCompositionMemoryEstimate RenderEngine::estimateHierarchyMemory(
    const Document &document, const QSize &outputSize)
{
    return LayerCompositionPlan::build(document).memoryEstimate(outputSize);
}

bool RenderEngine::supportsLayerSplit(
    const Document &document, const QUuid &layerId)
{
    const int targetIndex = document.layerIndex(layerId);
    const Layer *target = document.layer(layerId);
    if (targetIndex < 0 || !target || target->kind != LayerKind::Paint
        || std::any_of(
            document.layers.cbegin(),
            document.layers.cend(),
            [](const Layer &layer)
            {
                return layer.kind == LayerKind::Group
                       || !layer.parentGroupId.isNull()
                       || layer.clipToLayerBelow;
            }))
    {
        return false;
    }
    for (int index = targetIndex + 1; index < document.layers.size(); ++index)
    {
        const Layer &layer = document.layers[index];
        if (layer.visible && layer.opacity > 0.0 && !layer.strokes.isEmpty()
            && layer.blendMode != LayerBlendMode::Normal)
        {
            return false;
        }
    }
    return true;
}

RenderEngine::LayerSplitFrame RenderEngine::renderLayerSplit(
    const Document &document,
    int frameIndex,
    const QSize &outputSize,
    const QUuid &layerId,
    ScaledRenderMode mode,
    ScaledRenderStats *stats)
{
    if (stats)
    {
        *stats = {};
    }
    LayerSplitFrame split;
    if (document.size.isEmpty() || outputSize.isEmpty()
        || !supportsLayerSplit(document, layerId))
    {
        return split;
    }

    QImage below(outputSize, QImage::Format_ARGB32_Premultiplied);
    QImage layerBase(outputSize, QImage::Format_ARGB32_Premultiplied);
    if (below.isNull() || layerBase.isNull())
    {
        return split;
    }
    below.fill(document.background);
    layerBase.fill(Qt::transparent);
    QImage above;

    const int frameCount = std::max(1, document.animationFrames);
    const int normalizedFrame =
        ((frameIndex % frameCount) + frameCount) % frameCount;

    const bool displayScaleReplay =
        mode == ScaledRenderMode::DisplayPreview
        && canReplayAtDisplayScale(document, outputSize);
    if (stats)
    {
        stats->usedDisplayScaleReplay = displayScaleReplay;
        stats->usedNativeExactFallback = !displayScaleReplay;
    }
    ScaledRenderStats *const previewStats =
        displayScaleReplay ? stats : nullptr;
    const PreviewScaleMapping mapping{document.size,
        outputSize,
        static_cast<qreal>(outputSize.width()) / document.size.width(),
        static_cast<qreal>(outputSize.height()) / document.size.height()};
    notePreviewImage(previewStats, below);
    notePreviewImage(previewStats, layerBase);
    notePreviewWorkingSet(previewStats, below, layerBase);

    const auto renderedLayer = [&document,
                                   &mapping,
                                   previewStats,
                                   displayScaleReplay,
                                   outputSize,
                                   normalizedFrame,
                                   frameCount](const Layer &layer)
    {
        QImage native;
        const QSize initialSize = layer.initialCanvasSize.isValid()
                                      ? layer.initialCanvasSize
                                      : DocumentOperations::initialCanvasSize(
                                            layer.strokes, document.size);
        if (displayScaleReplay)
        {
            QImage displayLayer;
            if (!renderLayerOperationsAtDisplayScale(displayLayer,
                    document,
                    layer.strokes,
                    normalizedFrame,
                    frameCount,
                    initialSize,
                    mapping,
                    previewStats))
            {
                return QImage();
            }
            return displayLayer;
        }
        if (!renderLayerOperations(native,
                document,
                layer.strokes,
                normalizedFrame,
                frameCount,
                initialSize))
        {
            return QImage();
        }
        return native.size() == outputSize ? native
                                           : native.scaled(outputSize,
                                                 Qt::IgnoreAspectRatio,
                                                 Qt::FastTransformation);
    };

    bool afterTarget = false;
    for (const Layer &layer : document.layers)
    {
        if (layer.id == layerId)
        {
            split.layerVisible = layer.visible && layer.opacity > 0.0;
            split.layerOpacity = std::clamp(layer.opacity, 0.0, 1.0);
            split.layerBlendMode = layer.blendMode;
            if (split.layerVisible && !layer.strokes.isEmpty())
            {
                layerBase = renderedLayer(layer);
                if (layerBase.isNull())
                {
                    return split;
                }
            }
            afterTarget = true;
            continue;
        }
        if (!layer.visible || layer.opacity <= 0.0 || layer.strokes.isEmpty())
        {
            continue;
        }

        const QImage layerImage = renderedLayer(layer);
        if (layerImage.isNull())
        {
            return split;
        }
        if (afterTarget && above.isNull())
        {
            above = QImage(outputSize, QImage::Format_ARGB32_Premultiplied);
            if (above.isNull())
            {
                return split;
            }
            above.fill(Qt::transparent);
            notePreviewImage(previewStats, above);
        }
        notePreviewWorkingSet(
            previewStats, below, layerBase, above, layerImage);
        QPainter compositor(afterTarget ? &above : &below);
        compositor.setRenderHint(QPainter::Antialiasing, false);
        prepareLayerComposition(compositor, layer.blendMode, layer.opacity);
        compositor.drawImage(QPoint(0, 0), layerImage);
    }

    split.below = std::move(below);
    split.layerBase = std::move(layerBase);
    split.above = std::move(above);
    split.valid = true;
    return split;
}

RenderEngine::LayerRasterFrame RenderEngine::renderLayerRasterFrame(
    const Document &document,
    int frameIndex,
    const QSize &outputSize,
    qint64 maximumBytes,
    ScaledRenderMode mode,
    ScaledRenderStats *stats)
{
    if (stats)
    {
        *stats = {};
    }
    LayerRasterFrame frame;
    if (document.size.isEmpty() || outputSize.isEmpty() || maximumBytes <= 0)
    {
        return frame;
    }
    frame.outputSize = outputSize;

    const int frameCount = std::max(1, document.animationFrames);
    const int normalizedFrame =
        ((frameIndex % frameCount) + frameCount) % frameCount;
    const bool displayScaleReplay =
        mode == ScaledRenderMode::DisplayPreview
        && canReplayAtDisplayScale(document, outputSize);
    if (stats)
    {
        stats->usedDisplayScaleReplay = displayScaleReplay;
        stats->usedNativeExactFallback = !displayScaleReplay;
    }
    ScaledRenderStats *const previewStats =
        displayScaleReplay ? stats : nullptr;
    const PreviewScaleMapping mapping{document.size,
        outputSize,
        static_cast<qreal>(outputSize.width()) / document.size.width(),
        static_cast<qreal>(outputSize.height()) / document.size.height()};

    QImage sharedEmpty;
    qint64 retainedBytes = 0;
    for (const Layer &layer : document.layers)
    {
        if (layer.kind != LayerKind::Paint)
        {
            continue;
        }

        QImage layerImage;
        if (layer.strokes.isEmpty())
        {
            if (sharedEmpty.isNull())
            {
                sharedEmpty =
                    QImage(outputSize, QImage::Format_ARGB32_Premultiplied);
                if (sharedEmpty.isNull())
                {
                    return frame;
                }
                sharedEmpty.fill(Qt::transparent);
                const qint64 imageBytes = sharedEmpty.sizeInBytes();
                if (imageBytes > maximumBytes - retainedBytes)
                {
                    return frame;
                }
                retainedBytes += imageBytes;
                notePreviewImage(previewStats, sharedEmpty);
            }
            layerImage = sharedEmpty;
        }
        else
        {
            const QSize initialSize =
                layer.initialCanvasSize.isValid()
                    ? layer.initialCanvasSize
                    : DocumentOperations::initialCanvasSize(
                          layer.strokes, document.size);
            if (displayScaleReplay)
            {
                if (!renderLayerOperationsAtDisplayScale(layerImage,
                        document,
                        layer.strokes,
                        normalizedFrame,
                        frameCount,
                        initialSize,
                        mapping,
                        previewStats))
                {
                    return frame;
                }
            }
            else
            {
                QImage native;
                if (!renderLayerOperations(native,
                        document,
                        layer.strokes,
                        normalizedFrame,
                        frameCount,
                        initialSize))
                {
                    return frame;
                }
                layerImage = native.size() == outputSize
                                 ? native
                                 : native.scaled(outputSize,
                                       Qt::IgnoreAspectRatio,
                                       Qt::FastTransformation);
                if (layerImage.isNull())
                {
                    return frame;
                }
            }
            const qint64 imageBytes = layerImage.sizeInBytes();
            if (imageBytes > maximumBytes - retainedBytes)
            {
                return frame;
            }
            retainedBytes += imageBytes;
        }
        if (retainedBytes > maximumBytes)
        {
            return frame;
        }
        frame.paintLayers.insert(layer.id, std::move(layerImage));
    }
    frame.valid = true;
    return frame;
}

QImage RenderEngine::composeLayerRasterFrame(const Document &document,
    const LayerRasterFrame &frame,
    const QUuid &replacementLayerId,
    const QImage &replacementLayer)
{
    if (!frame.valid || !frame.outputSize.isValid()
        || (!replacementLayerId.isNull()
            && (replacementLayer.isNull()
                || replacementLayer.size() != frame.outputSize)))
    {
        return {};
    }
    const Layer *replacement = document.layer(replacementLayerId);
    if (!replacementLayerId.isNull()
        && (!replacement || replacement->kind != LayerKind::Paint))
    {
        return {};
    }
    const auto renderPaintLayer = [&](const Layer &layer)
    {
        if (layer.id == replacementLayerId)
        {
            return replacementLayer;
        }
        const auto cached = frame.paintLayers.constFind(layer.id);
        return cached == frame.paintLayers.cend() ? QImage() : cached.value();
    };
    return renderLayerHierarchy(document,
        frame.outputSize,
        document.background,
        renderPaintLayer,
        nullptr);
}

QImage RenderEngine::composeLayerRasterFrameRegion(const Document &document,
    const LayerRasterFrame &frame,
    const QUuid &replacementLayerId,
    const QImage &replacementLayer,
    const QRect &outputRegion)
{
    if (!frame.valid || frame.outputSize.isEmpty() || outputRegion.isEmpty()
        || !QRect(QPoint(), frame.outputSize).contains(outputRegion)
        || (!replacementLayerId.isNull()
            && (replacementLayer.isNull()
                || replacementLayer.size() != outputRegion.size()
                || replacementLayer.format()
                       != QImage::Format_ARGB32_Premultiplied)))
    {
        return {};
    }
    for (auto layer = frame.paintLayers.cbegin();
        layer != frame.paintLayers.cend();
        ++layer)
    {
        if (layer.key() == replacementLayerId)
        {
            continue;
        }
        const QImage &source = layer.value();
        if (source.isNull() || source.size() != frame.outputSize
            || source.format() != QImage::Format_ARGB32_Premultiplied)
        {
            return {};
        }
    }
    if (outputRegion == QRect(QPoint(), frame.outputSize))
    {
        return composeLayerRasterFrame(
            document, frame, replacementLayerId, replacementLayer);
    }
    LayerRasterFrame regionalFrame;
    regionalFrame.outputSize = outputRegion.size();
    QHash<qint64, QImage> regionalCopies;
    for (auto layer = frame.paintLayers.cbegin();
        layer != frame.paintLayers.cend();
        ++layer)
    {
        if (layer.key() == replacementLayerId)
        {
            continue;
        }
        const QImage &source = layer.value();
        const qint64 cacheKey = source.cacheKey();
        auto region = regionalCopies.constFind(cacheKey);
        if (region == regionalCopies.cend())
        {
            QImage copy = source.copy(outputRegion);
            if (copy.isNull())
            {
                return {};
            }
            region = regionalCopies.insert(cacheKey, std::move(copy));
        }
        regionalFrame.paintLayers.insert(layer.key(), region.value());
    }
    regionalFrame.valid = true;
    return composeLayerRasterFrame(
        document, regionalFrame, replacementLayerId, replacementLayer);
}

QImage RenderEngine::composeLayerSplit(
    const LayerSplitFrame &split, const QImage &layerImage)
{
    if (!split.valid || split.below.isNull())
    {
        return {};
    }
    QImage result = split.below;
    QPainter compositor(&result);
    compositor.setRenderHint(QPainter::Antialiasing, false);
    if (split.layerVisible && !layerImage.isNull())
    {
        prepareLayerComposition(
            compositor, split.layerBlendMode, split.layerOpacity);
        compositor.drawImage(QPoint(0, 0), layerImage);
    }
    if (!split.above.isNull())
    {
        prepareLayerComposition(compositor, LayerBlendMode::Normal, 1.0);
        compositor.drawImage(QPoint(0, 0), split.above);
    }
    return result;
}

QImage RenderEngine::composeLayerSplitRegion(const LayerSplitFrame &split,
    const QImage &layerImage,
    const QRect &outputRegion)
{
    if (!split.valid || split.below.isNull()
        || split.below.format() != QImage::Format_ARGB32_Premultiplied
        || outputRegion.isEmpty()
        || !QRect(QPoint(), split.below.size()).contains(outputRegion)
        || (split.layerVisible
            && (layerImage.isNull() || layerImage.size() != outputRegion.size()
                || layerImage.format() != QImage::Format_ARGB32_Premultiplied))
        || (!split.above.isNull()
            && (split.above.size() != split.below.size()
                || split.above.format()
                       != QImage::Format_ARGB32_Premultiplied)))
    {
        return {};
    }
    QImage result = split.below.copy(outputRegion);
    if (result.isNull())
    {
        return {};
    }
    QPainter compositor(&result);
    compositor.setRenderHint(QPainter::Antialiasing, false);
    if (split.layerVisible)
    {
        prepareLayerComposition(
            compositor, split.layerBlendMode, split.layerOpacity);
        compositor.drawImage(QPoint(), layerImage);
    }
    if (!split.above.isNull())
    {
        prepareLayerComposition(compositor, LayerBlendMode::Normal, 1.0);
        compositor.drawImage(QPoint(), split.above, outputRegion);
    }
    return result;
}

bool RenderEngine::replayPixelSelectionOnLayer(QImage &layerImage,
    const PixelSelectionOp &operation,
    ScaledRenderMode mode,
    ScaledRenderStats *stats)
{
    if (stats)
    {
        *stats = {};
    }
    if (!isValidPixelSelectionOp(operation) || layerImage.isNull()
        || layerImage.format() != QImage::Format_ARGB32_Premultiplied)
    {
        return false;
    }
    if (mode == ScaledRenderMode::DisplayPreview
        && isNonUpscaledDisplaySize(operation.canvasSize, layerImage.size()))
    {
        if (stats)
        {
            stats->usedDisplayScaleReplay = true;
        }
        const PreviewScaleMapping mapping{operation.canvasSize,
            layerImage.size(),
            static_cast<qreal>(layerImage.width())
                / operation.canvasSize.width(),
            static_cast<qreal>(layerImage.height())
                / operation.canvasSize.height()};
        notePreviewImage(stats, layerImage);
        notePreviewWorkingSet(stats, layerImage);
        return applyPixelSelectionOperationAtDisplayScale(
            layerImage, operation, mapping, operation.canvasSize, stats);
    }
    if (layerImage.size() != operation.canvasSize)
    {
        return false;
    }
    if (stats)
    {
        stats->usedNativeExactFallback = true;
        ++stats->pixelSelectionOperationsReplayed;
    }
    return applyPixelSelectionOperation(layerImage, operation);
}

RenderEngine::PixelSelectionPreviewRegion
RenderEngine::replayPixelSelectionOnLayerRegion(
    const QImage &layerImage, const PixelSelectionOp &operation)
{
    PixelSelectionPreviewRegion result;
    if (!isValidPixelSelectionOp(operation) || layerImage.isNull()
        || layerImage.format() != QImage::Format_ARGB32_Premultiplied
        || layerImage.width() > operation.canvasSize.width()
        || layerImage.height() > operation.canvasSize.height())
    {
        return result;
    }
    const PreviewScaleMapping mapping{operation.canvasSize,
        layerImage.size(),
        static_cast<qreal>(layerImage.width()) / operation.canvasSize.width(),
        static_cast<qreal>(layerImage.height())
            / operation.canvasSize.height()};
    DisplaySelectionMask selection;
    if (!buildDisplaySelectionMask(selection,
            operation,
            mapping,
            operation.canvasSize,
            layerImage.size(),
            nullptr))
    {
        return result;
    }
    if (selection.bounds.isEmpty())
    {
        result.valid = true;
        return result;
    }

    const QTransform transform = mapping.displayTransform(operation.transform);
    const QRect destinationBounds =
        operation.drawDestination
            ? ImageAffineTransformer::targetBounds(selection.bounds,
                  layerImage.size(),
                  transform,
                  operation.sampling)
            : QRect();
    result.bounds = (operation.clearSource ? selection.bounds : QRect())
                        .united(destinationBounds)
                        .intersected(QRect(QPoint(), layerImage.size()));
    if (result.bounds.isEmpty())
    {
        result.valid = true;
        return result;
    }
    result.image = layerImage.copy(result.bounds);
    if (result.image.isNull())
    {
        return {};
    }

    QImage payload;
    if (operation.drawDestination)
    {
        payload = layerImage.copy(selection.bounds);
        if (payload.isNull())
        {
            return {};
        }
    }
    for (int y = selection.bounds.top(); y <= selection.bounds.bottom(); ++y)
    {
        const uchar *maskLine =
            selection.mask.constScanLine(y - selection.bounds.top());
        QRgb *payloadLine = payload.isNull()
                                ? nullptr
                                : reinterpret_cast<QRgb *>(payload.scanLine(
                                      y - selection.bounds.top()));
        QRgb *resultLine = result.bounds.contains(selection.bounds.left(), y)
                               ? reinterpret_cast<QRgb *>(result.image.scanLine(
                                     y - result.bounds.top()))
                               : nullptr;
        for (int x = selection.bounds.left(); x <= selection.bounds.right();
            ++x)
        {
            if (maskLine[x - selection.bounds.left()] >= 128)
            {
                if (operation.clearSource && result.bounds.contains(x, y))
                {
                    resultLine[x - result.bounds.left()] = 0;
                }
            }
            else if (payloadLine)
            {
                payloadLine[x - selection.bounds.left()] = 0;
            }
        }
    }
    if (operation.drawDestination
        && !ImageAffineTransformer::compositeSourceOver(result.image,
            result.bounds,
            payload,
            selection.bounds,
            transform,
            operation.sampling))
    {
        return {};
    }
    result.valid = true;
    return result;
}

}
