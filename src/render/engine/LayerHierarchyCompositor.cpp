#include "render/engine/LayerHierarchyCompositor.hpp"

#include "document/DocumentOperations.hpp"
#include "render/ImageResampler.hpp"
#include "render/engine/DisplayScaleReplay.hpp"
#include "render/engine/LayerOperationReplay.hpp"

#include <QHash>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace wobble
{
namespace render_detail
{

QPainter::CompositionMode compositionMode(LayerBlendMode mode)
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

void prepareLayerComposition(
    QPainter &painter, LayerBlendMode mode, qreal opacity)
{
    painter.setCompositionMode(compositionMode(mode));
    painter.setOpacity(std::clamp(opacity, 0.0, 1.0));
}

QImage renderAtDisplayScale(const Document &document,
    int frameIndex,
    const QSize &outputSize,
    RenderEngine::ScaledRenderStats *stats)
{
    if (document.size.isEmpty() || outputSize.isEmpty())
    {
        return {};
    }
    const PreviewScaleMapping mapping{document.size,
        outputSize,
        static_cast<qreal>(outputSize.width()) / document.size.width(),
        static_cast<qreal>(outputSize.height()) / document.size.height()};

    const int frameCount = std::max(1, document.animationFrames);
    const int normalizedFrame =
        ((frameIndex % frameCount) + frameCount) % frameCount;
    const auto renderPaintLayer = [&](const Layer &layer)
    {
        if (layer.strokes.isEmpty())
        {
            QImage empty(outputSize, QImage::Format_ARGB32_Premultiplied);
            if (!empty.isNull())
            {
                empty.fill(Qt::transparent);
            }
            return empty;
        }
        const QSize initialSize = layer.initialCanvasSize.isValid()
                                      ? layer.initialCanvasSize
                                      : DocumentOperations::initialCanvasSize(
                                            layer.strokes, document.size);
        QImage layerImage;
        if (!renderLayerOperationsAtDisplayScale(layerImage,
                document,
                layer.strokes,
                normalizedFrame,
                frameCount,
                initialSize,
                mapping,
                stats))
        {
            return QImage();
        }
        return layerImage;
    };
    return renderLayerHierarchy(
        document, outputSize, document.background, renderPaintLayer, stats);
}

QImage renderAtSize(
    const Document &document, int frameIndex, const QSize &outputSize)
{
    if (document.size.isEmpty() || outputSize.isEmpty())
    {
        return {};
    }

    const int frameCount = std::max(1, document.animationFrames);
    const int normalizedFrame =
        ((frameIndex % frameCount) + frameCount) % frameCount;

    const auto renderPaintLayer = [&](const Layer &layer)
    {
        if (layer.strokes.isEmpty())
        {
            QImage empty(outputSize, QImage::Format_ARGB32_Premultiplied);
            if (!empty.isNull())
            {
                empty.fill(Qt::transparent);
            }
            return empty;
        }

        QImage nativeLayer;
        const QSize initialSize = layer.initialCanvasSize.isValid()
                                      ? layer.initialCanvasSize
                                      : DocumentOperations::initialCanvasSize(
                                            layer.strokes, document.size);
        if (!renderLayerOperations(nativeLayer,
                document,
                layer.strokes,
                normalizedFrame,
                frameCount,
                initialSize))
        {
            return QImage();
        }
        QImage layerImage = nativeLayer.size() == outputSize
                                ? nativeLayer
                                : nativeLayer.scaled(outputSize,
                                      Qt::IgnoreAspectRatio,
                                      Qt::FastTransformation);
        if (layerImage.isNull())
        {
            return QImage();
        }
        return layerImage;
    };
    return renderLayerHierarchy(
        document, outputSize, document.background, renderPaintLayer, nullptr);
}

}

}
