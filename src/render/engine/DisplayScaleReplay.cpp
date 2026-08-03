#include "render/engine/DisplayScaleReplay.hpp"

#include "document/DocumentOperations.hpp"
#include "document/SelectionOperation.hpp"
#include "render/ImageAffineTransformer.hpp"
#include "render/ImageResampler.hpp"
#include "render/engine/LayerOperationReplay.hpp"

#include <QHash>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace wobble
{
namespace render_detail
{

bool buildDisplaySelectionMask(DisplaySelectionMask &result,
    const PixelSelectionOp &operation,
    const PreviewScaleMapping &mapping,
    const QSize &nativeCanvasSize,
    const QSize &displayCanvasSize,
    RenderEngine::ScaledRenderStats *stats)
{
    if (!isValidPixelSelectionOp(operation)
        || operation.canvasSize != nativeCanvasSize
        || mapping.displaySize(nativeCanvasSize) != displayCanvasSize)
    {
        return false;
    }
    result.bounds = mapping.displayBounds(
        operation.sourceBounds, QRect(QPoint(), displayCanvasSize));
    if (result.bounds.isEmpty())
    {
        return true;
    }
    result.mask = QImage(result.bounds.size(), QImage::Format_Grayscale8);
    if (result.mask.isNull())
    {
        return false;
    }
    result.mask.fill(0);
    notePreviewImage(stats, result.mask);
    for (int y = result.bounds.top(); y <= result.bounds.bottom(); ++y)
    {
        uchar *line = result.mask.scanLine(y - result.bounds.top());
        for (int x = result.bounds.left(); x <= result.bounds.right(); ++x)
        {
            const QPoint native =
                mapping.nativeSampleForDisplayPixel(QPoint(x, y));
            if (stats)
            {
                ++stats->packedSelectionSamples;
            }
            if (pixelSelectionContains(operation, native.x(), native.y()))
            {
                line[x - result.bounds.left()] = 255;
            }
        }
    }
    return true;
}

bool applyPixelSelectionOperationAtDisplayScale(QImage &layerImage,
    const PixelSelectionOp &operation,
    const PreviewScaleMapping &mapping,
    const QSize &nativeCanvasSize,
    RenderEngine::ScaledRenderStats *stats)
{
    if (layerImage.format() != QImage::Format_ARGB32_Premultiplied
        || (!operation.drawDestination && !operation.clearSource))
    {
        return false;
    }
    if (stats)
    {
        ++stats->pixelSelectionOperationsReplayed;
    }
    DisplaySelectionMask selection;
    if (!buildDisplaySelectionMask(selection,
            operation,
            mapping,
            nativeCanvasSize,
            layerImage.size(),
            stats))
    {
        return false;
    }
    if (selection.mask.isNull())
    {
        return true;
    }

    QImage payload;
    if (operation.drawDestination)
    {
        payload = QImage(
            selection.bounds.size(), QImage::Format_ARGB32_Premultiplied);
        if (payload.isNull())
        {
            return false;
        }
        payload.fill(Qt::transparent);
        notePreviewImage(stats, payload);
        notePreviewWorkingSet(stats, layerImage, selection.mask, payload);
    }
    else
    {
        notePreviewWorkingSet(stats, layerImage, selection.mask);
    }

    bool hasPixels = false;
    for (int y = selection.bounds.top(); y <= selection.bounds.bottom(); ++y)
    {
        QRgb *layerLine = reinterpret_cast<QRgb *>(layerImage.scanLine(y));
        const uchar *maskLine =
            selection.mask.constScanLine(y - selection.bounds.top());
        QRgb *payloadLine = payload.isNull()
                                ? nullptr
                                : reinterpret_cast<QRgb *>(payload.scanLine(
                                      y - selection.bounds.top()));
        for (int x = selection.bounds.left(); x <= selection.bounds.right();
            ++x)
        {
            const int localX = x - selection.bounds.left();
            if (maskLine[localX] < 128)
            {
                continue;
            }
            const QRgb pixel = layerLine[x];
            if (payloadLine)
            {
                payloadLine[localX] = pixel;
                hasPixels = hasPixels || qAlpha(pixel) != 0;
            }
            if (operation.clearSource)
            {
                layerLine[x] = 0;
            }
        }
    }
    if (!operation.drawDestination || !hasPixels)
    {
        return true;
    }

    QPainter painter(&layerImage);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setRenderHint(QPainter::SmoothPixmapTransform,
        operation.sampling == SamplingMode::Smooth);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    painter.setTransform(mapping.displayTransform(operation.transform));
    painter.drawImage(selection.bounds.topLeft(), payload);
    painter.end();
    return true;
}

bool applyReframeOperationAtDisplayScale(QImage &layerImage,
    const ReframeOp &operation,
    const PreviewScaleMapping &mapping,
    const QSize &nativeCanvasSize,
    RenderEngine::ScaledRenderStats *stats)
{
    if (!isValidReframeOp(operation) || operation.sourceSize != nativeCanvasSize
        || layerImage.size() != mapping.displaySize(nativeCanvasSize)
        || layerImage.format() != QImage::Format_ARGB32_Premultiplied)
    {
        return false;
    }
    const QSize targetDisplaySize = mapping.displaySize(operation.targetSize);
    QImage target(targetDisplaySize, QImage::Format_ARGB32_Premultiplied);
    if (target.isNull())
    {
        return false;
    }
    target.fill(Qt::transparent);
    notePreviewImage(stats, target);
    notePreviewWorkingSet(stats, layerImage, target);

    QPainter painter(&target);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setRenderHint(QPainter::SmoothPixmapTransform,
        operation.sampling == SamplingMode::Smooth);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    if (operation.mode == ReframeMode::Canvas)
    {
        painter.drawImage(
            mapping.displayPoint(operation.contentOffset), layerImage);
    }
    else
    {
        painter.drawImage(QRectF(QPointF(), QSizeF(targetDisplaySize)),
            layerImage,
            QRectF(QPointF(), QSizeF(layerImage.size())));
    }
    painter.end();
    layerImage = std::move(target);
    return true;
}

bool renderLayerOperationsAtDisplayScale(QImage &layerImage,
    const Document &document,
    const QVector<Stroke> &operations,
    int normalizedFrame,
    int frameCount,
    const QSize &initialCanvasSize,
    const PreviewScaleMapping &mapping,
    RenderEngine::ScaledRenderStats *stats)
{
    if (!initialCanvasSize.isValid())
    {
        return false;
    }
    layerImage = QImage(mapping.displaySize(initialCanvasSize),
        QImage::Format_ARGB32_Premultiplied);
    if (layerImage.isNull())
    {
        return false;
    }
    layerImage.fill(Qt::transparent);
    notePreviewImage(stats, layerImage);
    notePreviewWorkingSet(stats, layerImage);

    QSize nativeCanvasSize = initialCanvasSize;
    QHash<qint64, QPainterPath> clipPaths;
    QHash<qint64, QImage> scaledClipMasks;
    QVector<Stroke> primitiveRun;
    const auto flush = [&]()
    {
        if (primitiveRun.isEmpty())
        {
            return;
        }
        if (stats)
        {
            stats->primitiveStrokesRendered +=
                static_cast<quint64>(primitiveRun.size());
        }
        renderLayerStrokes(layerImage,
            document,
            primitiveRun,
            normalizedFrame,
            frameCount,
            mapping.horizontalScale,
            mapping.verticalScale,
            clipPaths,
            scaledClipMasks);
        primitiveRun.clear();
        notePreviewImage(stats, layerImage);
    };

    for (const Stroke &operation : operations)
    {
        if (operation.mode == StrokeMode::PixelSelection)
        {
            flush();
            if (!operation.pixelSelectionOp || operation.reframeOp
                || !applyPixelSelectionOperationAtDisplayScale(layerImage,
                    *operation.pixelSelectionOp,
                    mapping,
                    nativeCanvasSize,
                    stats))
            {
                return false;
            }
        }
        else if (operation.mode == StrokeMode::Reframe)
        {
            flush();
            if (!operation.reframeOp || operation.pixelSelectionOp
                || !applyReframeOperationAtDisplayScale(layerImage,
                    *operation.reframeOp,
                    mapping,
                    nativeCanvasSize,
                    stats))
            {
                return false;
            }
            nativeCanvasSize = operation.reframeOp->targetSize;
            // Cached masks and paths belong to the previous framebuffer
            // epoch and therefore to a different display surface.
            clipPaths.clear();
            scaledClipMasks.clear();
        }
        else
        {
            if (operation.pixelSelectionOp || operation.reframeOp)
            {
                return false;
            }
            primitiveRun.append(operation);
        }
    }
    flush();
    return nativeCanvasSize == document.size
           && layerImage.size() == mapping.outputSize;
}

}

}
