#pragma once

#include "ui/CanvasWidget.hpp"

#include <QSize>

namespace ugurugu
{

class CanvasWidgetTestAccess final
{
public:
    static QSize previewRenderSize(const CanvasWidget &canvas)
    {
        return canvas.previewRenderSize();
    }

    static CanvasWidget::DisplayedFrame resolveDisplayedFrame(
        CanvasWidget &canvas)
    {
        return canvas.resolveDisplayedFrame();
    }

    static bool usingGpuDisplay(const CanvasWidget &canvas)
    {
        return canvas.usingGpuDisplay();
    }

    static QSize cachedRenderSize(const CanvasWidget &canvas)
    {
        return canvas.m_cachedRenderSize;
    }

    static Document displayDocument(const CanvasWidget &canvas)
    {
        return canvas.displayDocument();
    }

    static bool zoomRenderPending(const CanvasWidget &canvas)
    {
        return canvas.m_zoomRenderTimer.isActive();
    }

    static bool hasCachedFrame(const CanvasWidget &canvas, int frame)
    {
        return canvas.m_frameCache.object(frame) != nullptr;
    }

    static bool frameCacheWarmupActive(const CanvasWidget &canvas)
    {
        return canvas.m_frameCacheWarmupActive;
    }

    static qsizetype cachedFrameCount(const CanvasWidget &canvas)
    {
        return canvas.m_frameCache.size();
    }

    static PreviewSurfaceUsage previewSurfaceUsage(const CanvasWidget &canvas)
    {
        return canvas.previewSurfaceUsage();
    }

    static QImage selectionMask(const CanvasWidget &canvas)
    {
        return canvas.m_selectionMask;
    }

    static bool drawing(const CanvasWidget &canvas)
    {
        return canvas.m_drawing;
    }

    static bool areaSelectionActive(const CanvasWidget &canvas)
    {
        return canvas.m_areaSelectionActive;
    }

    static QPointF mapToDocument(
        const CanvasWidget &canvas, const QPointF &widgetPosition)
    {
        return canvas.mapToDocument(widgetPosition);
    }

    static QPointF mapFromDocument(
        const CanvasWidget &canvas, const QPointF &documentPosition)
    {
        return canvas.documentTransform().map(documentPosition);
    }
};

}
