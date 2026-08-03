#pragma once

#include "ui/CanvasWidget.hpp"

#include <QSize>

namespace wobble
{

class CanvasWidgetTestAccess final
{
public:
    static QSize previewRenderSize(const CanvasWidget &canvas)
    {
        return canvas.previewRenderSize();
    }

    static QSize cachedRenderSize(const CanvasWidget &canvas)
    {
        return canvas.m_cachedRenderSize;
    }

    static bool zoomRenderPending(const CanvasWidget &canvas)
    {
        return canvas.m_zoomRenderTimer.isActive();
    }

    static bool hasCachedFrame(const CanvasWidget &canvas, int frame)
    {
        return canvas.m_frameCache.object(frame) != nullptr;
    }

    static PreviewSurfaceUsage previewSurfaceUsage(const CanvasWidget &canvas)
    {
        return canvas.previewSurfaceUsage();
    }
};

}
