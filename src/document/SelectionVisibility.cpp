#include "document/SelectionVisibility.hpp"

#include "render/RenderEngine.hpp"

#include <algorithm>

namespace wobble
{
namespace
{

QRect selectedBounds(const QImage &mask)
{
    int left = mask.width();
    int top = mask.height();
    int right = -1;
    int bottom = -1;
    for (int y = 0; y < mask.height(); ++y)
    {
        const uchar *line = mask.constScanLine(y);
        for (int x = 0; x < mask.width(); ++x)
        {
            if (line[x] < 128)
            {
                continue;
            }
            left = std::min(left, x);
            top = std::min(top, y);
            right = std::max(right, x);
            bottom = std::max(bottom, y);
        }
    }
    return right >= left && bottom >= top
               ? QRect(QPoint(left, top), QPoint(right, bottom))
               : QRect();
}

bool layerVariesByFrame(const Document &document, const Layer &layer)
{
    if (document.animationFrames <= 1)
    {
        return false;
    }
    return std::any_of(layer.strokes.cbegin(),
        layer.strokes.cend(),
        [&document](const Stroke &stroke)
        {
            if (stroke.mode == StrokeMode::Fill || stroke.pixelSelectionOp
                || stroke.reframeOp)
            {
                return false;
            }
            const bool displaced =
                document.wobbleAmount > 0.0 && stroke.brush.wobbleScale > 0.0;
            const bool animatedSpray = stroke.brush.engine == BrushEngine::Spray
                                       && stroke.brush.animatedJitter;
            return displaced || animatedSpray;
        });
}

bool intersectsVisiblePixels(
    const QImage &layerImage, const QImage &selectionMask, const QRect &bounds)
{
    for (int y = bounds.top(); y <= bounds.bottom(); ++y)
    {
        const auto *pixels =
            reinterpret_cast<const QRgb *>(layerImage.constScanLine(y));
        const uchar *selection = selectionMask.constScanLine(y);
        for (int x = bounds.left(); x <= bounds.right(); ++x)
        {
            if (selection[x] >= 128 && qAlpha(pixels[x]) != 0)
            {
                return true;
            }
        }
    }
    return false;
}

}

SelectionVisibility::Result SelectionVisibility::evaluate(
    const Document &document,
    const Layer &layer,
    const QImage &selectionMask,
    int preferredFrame)
{
    Result result;
    if (!document.size.isValid() || selectionMask.isNull()
        || selectionMask.size() != document.size
        || selectionMask.format() != QImage::Format_Grayscale8)
    {
        return result;
    }
    const QRect bounds = selectedBounds(selectionMask);
    if (bounds.isEmpty())
    {
        result.renderSucceeded = true;
        return result;
    }

    const int frameCount = std::max(1, document.animationFrames);
    const int normalizedPreferred =
        ((preferredFrame % frameCount) + frameCount) % frameCount;
    const bool animated = layerVariesByFrame(document, layer);
    const int framesToInspect = animated ? frameCount : 1;
    for (int offset = 0; offset < framesToInspect; ++offset)
    {
        const int frame = (normalizedPreferred + offset) % frameCount;
        QImage layerImage;
        ++result.renderedFrames;
        if (!RenderEngine::renderStrokesOnLayer(
                layerImage, document, layer.strokes, frame, document.size)
            || layerImage.size() != selectionMask.size())
        {
            return result;
        }
        if (intersectsVisiblePixels(layerImage, selectionMask, bounds))
        {
            result.hasVisiblePixels = true;
            result.renderSucceeded = true;
            return result;
        }
    }
    result.renderSucceeded = true;
    return result;
}

}
