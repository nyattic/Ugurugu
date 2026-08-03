#include "document/SelectionOperation.hpp"
#include "render/PreviewRenderPolicy.hpp"
#include "render/RenderEngine.hpp"
#include "ui/CanvasViewport.hpp"
#include "ui/CanvasWidget.hpp"
#include "ui/SelectionActionBar.hpp"
#include "ui/Theme.hpp"

#include <QEnterEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QTabletEvent>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace wobble
{

using namespace canvas_detail;

bool CanvasWidget::event(QEvent *event)
{
    if (event->type() == QEvent::NativeGesture)
    {
        auto *gesture = static_cast<QNativeGestureEvent *>(event);
        if (gesture->gestureType() == Qt::ZoomNativeGesture)
        {
            zoomToward(
                m_zoom * std::pow(2.0, gesture->value()), gesture->position());
            event->accept();
            return true;
        }
    }
    switch (event->type())
    {
    case QEvent::FocusOut:
    case QEvent::UngrabMouse:
    case QEvent::TabletLeaveProximity:
    case QEvent::WindowDeactivate:
        cancelActiveInteraction();
        break;
    default:
        break;
    }
    return QWidget::event(event);
}

void CanvasWidget::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.fillRect(rect(), Theme::canvasBackground());
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);

    const Document document = displayDocument();
    const QTransform transform = documentTransform();
    const QRectF canvasRect =
        transform.mapRect(QRectF(QPointF(0.0, 0.0), QSizeF(document.size)));

    const QRegion shadowRegion = event->region().subtracted(
        QRegion(canvasRect.toAlignedRect().intersected(rect())));
    if (!shadowRegion.isEmpty())
    {
        painter.save();
        painter.setClipRegion(shadowRegion);
        painter.setPen(Qt::NoPen);
        for (int step = 14; step > 0; --step)
        {
            QColor shadow(Qt::black);
            shadow.setAlphaF(0.020 * (1.0 - step / 14.0));
            painter.setBrush(shadow);
            painter.drawRoundedRect(
                canvasRect.adjusted(-step, -step + 2.0, step, step + 2.0),
                step * 0.9,
                step * 0.9);
        }
        painter.restore();
    }
    painter.setBrush(Qt::NoBrush);

    const QRectF visibleCanvasRect = canvasRect.intersected(QRectF(rect()));
    const QRectF checkerRect =
        visibleCanvasRect.intersected(QRectF(event->rect()));
    if (!checkerRect.isEmpty())
    {
        painter.setBrushOrigin(canvasRect.topLeft());
        painter.fillRect(checkerRect, checkerBrush());
    }

    painter.save();
    painter.setTransform(transform);
    const QSize renderSize = previewRenderSize();
    QImage displayedFrame;
    bool activeStrokePreviewResolved = false;
    if (m_drawing && !m_activeStroke.points.isEmpty())
    {
        displayedFrame = activeStrokePreview(
            document, renderSize, activeStrokePreviewResolved);
    }
    else if (hasPendingSelectionTransform())
    {
        const auto useRegionalPreview =
            [this, &displayedFrame](
                const RenderEngine::PixelSelectionPreviewRegion &region,
                const QImage &composed)
        {
            const QImage baseFrame = frameImage(m_currentFrame);
            if (!region.valid || baseFrame.isNull()
                || (!region.bounds.isEmpty() && composed.isNull()))
            {
                return false;
            }
            if (m_composedPreviewFrame.size() != baseFrame.size()
                || m_composedPreviewBaseKey != baseFrame.cacheKey())
            {
                m_composedPreviewFrame = baseFrame.copy();
                m_composedPreviewBaseKey = baseFrame.cacheKey();
                m_composedSelectionPreviewRegion = {};
            }
            QPainter compositor(&m_composedPreviewFrame);
            compositor.setCompositionMode(QPainter::CompositionMode_Source);
            const QRect resetRegion =
                m_composedSelectionPreviewRegion.united(region.bounds);
            if (!resetRegion.isEmpty())
            {
                compositor.drawImage(
                    resetRegion.topLeft(), baseFrame, resetRegion);
            }
            if (!region.bounds.isEmpty())
            {
                compositor.drawImage(region.bounds.topLeft(), composed);
            }
            compositor.end();
            m_composedSelectionPreviewRegion = region.bounds;
            displayedFrame = m_composedPreviewFrame;
            return true;
        };
        const RenderEngine::LayerSplitFrame &split =
            previewSplit(m_selectionTransformSession.layer, renderSize);
        if (split.valid)
        {
            const RenderEngine::PixelSelectionPreviewRegion region =
                RenderEngine::replayPixelSelectionOnLayerRegion(split.layerBase,
                    m_selectionTransformSession.previewOperation);
            const QImage composed =
                region.bounds.isEmpty()
                    ? QImage()
                    : RenderEngine::composeLayerSplitRegion(
                          split, region.image, region.bounds);
            if (!useRegionalPreview(region, composed))
            {
                QImage layerImage = split.layerBase;
                if (RenderEngine::replayPixelSelectionOnLayer(layerImage,
                        m_selectionTransformSession.previewOperation))
                {
                    displayedFrame =
                        RenderEngine::composeLayerSplit(split, layerImage);
                }
            }
        }
        if (displayedFrame.isNull())
        {
            const RenderEngine::LayerRasterFrame &rasters =
                previewLayerRasters(renderSize);
            const auto cached = rasters.paintLayers.constFind(
                m_selectionTransformSession.layer);
            if (rasters.valid && cached != rasters.paintLayers.cend())
            {
                const RenderEngine::PixelSelectionPreviewRegion region =
                    RenderEngine::replayPixelSelectionOnLayerRegion(
                        cached.value(),
                        m_selectionTransformSession.previewOperation);
                const QImage composed =
                    region.bounds.isEmpty()
                        ? QImage()
                        : RenderEngine::composeLayerRasterFrameRegion(document,
                              rasters,
                              m_selectionTransformSession.layer,
                              region.image,
                              region.bounds);
                if (!useRegionalPreview(region, composed))
                {
                    QImage layerImage = cached.value();
                    if (RenderEngine::replayPixelSelectionOnLayer(layerImage,
                            m_selectionTransformSession.previewOperation))
                    {
                        displayedFrame =
                            RenderEngine::composeLayerRasterFrame(document,
                                rasters,
                                m_selectionTransformSession.layer,
                                layerImage);
                    }
                }
            }
        }
    }
    if (displayedFrame.isNull() && !activeStrokePreviewResolved
        && ((m_drawing && !m_activeStroke.points.isEmpty())
            || hasPendingSelectionTransform()))
    {
        displayedFrame = interactionPreview(document, renderSize);
    }
    if (displayedFrame.isNull())
    {
        displayedFrame = frameImage(m_currentFrame);
    }
    painter.drawImage(
        QRectF(QPointF(0.0, 0.0), QSizeF(document.size)), displayedFrame);
    painter.restore();

    painter.setPen(QPen(Theme::canvasBorder(), 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(canvasRect);

    if (!m_drawing && !documentHasStrokes(document))
    {
        painter.setPen(QColor(0x9A, 0x9E, 0xA6));
        QFont hintFont = painter.font();
        hintFont.setPointSizeF(hintFont.pointSizeF() * 0.95);
        painter.setFont(hintFont);
        painter.drawText(canvasRect.adjusted(0.0, 0.0, 0.0, -14.0),
            Qt::AlignHCenter | Qt::AlignBottom,
            tr("B Brush · E Eraser · Space Pan · Scroll or Ctrl+Space "
               "Zoom · P Play"));
    }

    drawSelectionOverlay(painter, transform);

    const bool pointerUsesEraser =
        m_tabletPointerEraser || m_tool == Tool::Eraser;
    if (m_pointerOverWidget && !m_panning && !m_spacePressed && !m_pickingColor
        && !m_groupSelectionActive
        && (m_tabletPointerEraser || m_tool == Tool::Brush
            || m_tool == Tool::Eraser))
    {
        const qreal toolWidth =
            pointerUsesEraser ? m_eraserWidth : m_brushWidth;
        const qreal radius =
            std::max(1.0, toolWidth * std::abs(transform.m11()) * 0.5);
        const QRectF footprint(m_pointerWidgetPosition.x() - radius,
            m_pointerWidgetPosition.y() - radius,
            radius * 2.0,
            radius * 2.0);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(20, 20, 20, 220), 3.0));
        painter.drawEllipse(footprint);
        QPen innerPen(QColor(250, 250, 250, 235), 1.0);
        if (pointerUsesEraser)
        {
            innerPen.setStyle(Qt::DashLine);
        }
        painter.setPen(innerPen);
        painter.drawEllipse(footprint);
    }
    updateSelectionActionBar();
}

void CanvasWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    invalidateActiveStrokePreview();
    notifyZoomChanged();
    updateSelectionActionBar();
    update();
}

void CanvasWidget::enterEvent(QEnterEvent *event)
{
    m_pointerOverWidget = true;
    updatePointerPosition(event->position());
    QWidget::enterEvent(event);
}

void CanvasWidget::mousePressEvent(QMouseEvent *event)
{
    if (!m_tabletSequence && m_tabletPointerEraser)
    {
        const QRect pointerRect = pointerUpdateRect();
        m_tabletPointerEraser = false;
        updateCursor();
        if (!pointerRect.isEmpty())
        {
            update(pointerRect);
        }
    }
    updatePointerPosition(event->position());
    setFocus(Qt::MouseFocusReason);
    if (event->button() == Qt::LeftButton && m_spacePressed
        && event->modifiers().testFlag(Qt::ControlModifier))
    {
        beginZoomDrag(event->position());
        event->accept();
        return;
    }
    if (event->button() == Qt::MiddleButton
        || (event->button() == Qt::LeftButton && m_spacePressed))
    {
        beginPan(event->position());
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && !m_tabletSequence
        && event->modifiers().testFlag(Qt::AltModifier)
        && isColorPickableTool())
    {
        beginColorPick(event->position());
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && !m_tabletSequence)
    {
        const QPointF documentPosition = mapToDocument(event->position());
        if (m_selectionMoveMode)
        {
            if (selectionContains(documentPosition))
            {
                beginSelectionMove(documentPosition);
            }
            else
            {
                emit interactionMessage(
                    tr("Drag inside the selection to move it."));
            }
            event->accept();
            return;
        }
        const Document &document = m_controller->document();
        if (!document.layer(document.activeLayerId))
        {
            emit interactionMessage(tr("Add a layer before using this tool."));
            event->accept();
            return;
        }
        switch (m_tool)
        {
        case Tool::Brush:
        case Tool::Eraser:
            beginStroke(event->position(), 1.0, false, event->timestamp());
            break;
        case Tool::Lasso:
            beginAreaSelection(documentPosition);
            break;
        case Tool::Wand:
            computeWandSelection(documentPosition);
            break;
        case Tool::Bucket:
            applyBucketFill(documentPosition);
            break;
        }
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void CanvasWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_tabletSequence && m_tabletPointerEraser)
    {
        const QRect pointerRect = pointerUpdateRect();
        m_tabletPointerEraser = false;
        updateCursor();
        if (!pointerRect.isEmpty())
        {
            update(pointerRect);
        }
    }
    updatePointerPosition(event->position());
    if (m_zoomDragging)
    {
        continueZoomDrag(event->position());
        event->accept();
        return;
    }
    if (m_pickingColor && !m_tabletSequence)
    {
        pickColorAt(event->position());
        event->accept();
        return;
    }
    if (m_panning)
    {
        continuePan(event->position());
        event->accept();
        return;
    }
    if (m_drawing && !m_tabletSequence)
    {
        continueStroke(event->position(), 1.0, event->timestamp());
        event->accept();
        return;
    }
    if (m_movingSelection)
    {
        continueSelectionMove(mapToDocument(event->position()));
        event->accept();
        return;
    }
    if (m_areaSelectionActive)
    {
        continueAreaSelection(mapToDocument(event->position()));
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void CanvasWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (!m_tabletSequence && m_tabletPointerEraser)
    {
        const QRect pointerRect = pointerUpdateRect();
        m_tabletPointerEraser = false;
        updateCursor();
        if (!pointerRect.isEmpty())
        {
            update(pointerRect);
        }
    }
    updatePointerPosition(event->position());
    if (m_zoomDragging && event->button() == Qt::LeftButton)
    {
        endZoomDrag();
        event->accept();
        return;
    }
    if (m_pickingColor && !m_tabletSequence
        && event->button() == Qt::LeftButton)
    {
        endColorPick();
        event->accept();
        return;
    }
    if (m_panning
        && (event->button() == Qt::MiddleButton
            || event->button() == Qt::LeftButton))
    {
        endPan();
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && m_drawing && !m_tabletSequence)
    {
        endStroke(event->position(), event->timestamp());
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && m_movingSelection)
    {
        continueSelectionMove(mapToDocument(event->position()));
        commitSelectionMove();
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && m_areaSelectionActive)
    {
        continueAreaSelection(mapToDocument(event->position()));
        finishAreaSelection();
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void CanvasWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MiddleButton)
    {
        fitToWindow();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void CanvasWidget::wheelEvent(QWheelEvent *event)
{
    const QPointF cursor = event->position();
    updatePointerPosition(cursor);
    const qreal factor = std::pow(1.0015, event->angleDelta().y());
    zoomToward(m_zoom * factor, cursor);
    event->accept();
}

void CanvasWidget::tabletEvent(QTabletEvent *event)
{
    const QRect previousPointerRect = pointerUpdateRect();
    const bool eraser =
        event->pointerType() == QPointingDevice::PointerType::Eraser;
    m_tabletPointerEraser = eraser;
    updatePointerPosition(event->position());
    if (!previousPointerRect.isEmpty())
    {
        update(previousPointerRect);
    }
    updateCursor();
    if (event->type() == QEvent::TabletPress)
    {
        if (m_tabletSequence)
        {
            cancelActiveInteraction();
            m_tabletPointerEraser = eraser;
            updateCursor();
        }
        if (event->button() != Qt::LeftButton)
        {
            QWidget::tabletEvent(event);
            return;
        }
        if (m_spacePressed)
        {
            m_tabletSequence = true;
            if (event->modifiers().testFlag(Qt::ControlModifier))
            {
                beginZoomDrag(event->position());
            }
            else
            {
                beginPan(event->position());
            }
            event->accept();
            return;
        }
        if (event->modifiers().testFlag(Qt::AltModifier)
            && isColorPickableTool())
        {
            m_tabletSequence = true;
            beginColorPick(event->position());
            event->accept();
            return;
        }
        if (m_selectionMoveMode)
        {
            m_tabletSequence = true;
            const QPointF documentPosition = mapToDocument(event->position());
            if (selectionContains(documentPosition))
            {
                beginSelectionMove(documentPosition);
            }
            else
            {
                emit interactionMessage(
                    tr("Drag inside the selection to move it."));
            }
            event->accept();
            return;
        }
        m_tabletSequence = true;
        if (!eraser && m_tool == Tool::Lasso)
        {
            beginAreaSelection(mapToDocument(event->position()));
            event->accept();
            return;
        }
        if (!eraser && m_tool == Tool::Wand)
        {
            computeWandSelection(mapToDocument(event->position()));
            event->accept();
            return;
        }
        if (!eraser && m_tool == Tool::Bucket)
        {
            applyBucketFill(mapToDocument(event->position()));
            event->accept();
            return;
        }
        beginStroke(
            event->position(), event->pressure(), eraser, event->timestamp());
        event->accept();
        return;
    }
    if (event->type() == QEvent::TabletMove)
    {
        if (!m_tabletSequence)
        {
            event->accept();
            return;
        }
        if (m_zoomDragging)
        {
            continueZoomDrag(event->position());
        }
        else if (m_pickingColor)
        {
            pickColorAt(event->position());
        }
        else if (m_panning)
        {
            continuePan(event->position());
        }
        else if (m_movingSelection)
        {
            continueSelectionMove(mapToDocument(event->position()));
        }
        else if (m_areaSelectionActive)
        {
            continueAreaSelection(mapToDocument(event->position()));
        }
        else if (m_drawing)
        {
            continueStroke(
                event->position(), event->pressure(), event->timestamp());
        }
        event->accept();
        return;
    }
    if (event->type() == QEvent::TabletRelease && m_tabletSequence)
    {
        if (m_zoomDragging)
        {
            endZoomDrag();
        }
        else if (m_pickingColor)
        {
            endColorPick();
        }
        else if (m_panning)
        {
            endPan();
        }
        else if (m_movingSelection)
        {
            continueSelectionMove(mapToDocument(event->position()));
            commitSelectionMove();
        }
        else if (m_areaSelectionActive)
        {
            continueAreaSelection(mapToDocument(event->position()));
            finishAreaSelection();
        }
        else if (m_drawing)
        {
            endStroke(event->position(), event->timestamp());
        }
        m_tabletSequence = false;
        event->accept();
        return;
    }
    QWidget::tabletEvent(event);
}

void CanvasWidget::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat())
    {
        setPanModifierActive(true);
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape)
    {
        handleEscape();
        event->accept();
        return;
    }
    if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace)
        && !m_selectedStrokes.isEmpty())
    {
        deleteSelection();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void CanvasWidget::keyReleaseEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat())
    {
        setPanModifierActive(false);
        event->accept();
        return;
    }
    QWidget::keyReleaseEvent(event);
}

void CanvasWidget::leaveEvent(QEvent *event)
{
    const QRect pointerRect = pointerUpdateRect();
    m_pointerInside = false;
    m_pointerOverWidget = false;
    emit pointerPositionChanged(QPointF(), false);
    if (!pointerRect.isEmpty())
    {
        update(pointerRect);
    }
    QWidget::leaveEvent(event);
}
}
