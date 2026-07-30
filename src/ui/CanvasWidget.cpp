#include "ui/CanvasWidget.hpp"

#include "document/DocumentLimits.hpp"
#include "render/RenderEngine.hpp"
#include "ui/Theme.hpp"

#include <QEnterEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygonF>
#include <QRandomGenerator>
#include <QResizeEvent>
#include <QTabletEvent>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <limits>

namespace wobble {

namespace {

constexpr qreal canvasMargin = 32.0;

qreal pointDistance(const QPointF &a, const QPointF &b)
{
    return std::hypot(a.x() - b.x(), a.y() - b.y());
}

bool documentHasStrokes(const Document &document)
{
    for (const Layer &layer : document.layers) {
        if (!layer.strokes.isEmpty()) {
            return true;
        }
    }
    return false;
}

}

CanvasWidget::CanvasWidget(
    DocumentController *controller,
    QWidget *parent)
    : QWidget(parent)
    , m_controller(controller)
{
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_AcceptTouchEvents, false);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setCursor(Qt::BlankCursor);
    m_frameCache.setMaxCost(96 * 1024);

    connect(m_controller, &DocumentController::documentChanged, this, [this]() {
        invalidateFrames();
        pruneSelection();
    });
    connect(&m_animationTimer, &QTimer::timeout, this, [this]() {
        advanceFrame();
    });

    updateTimerInterval();
    m_animationTimer.start();
}

CanvasWidget::Tool CanvasWidget::tool() const
{
    return m_tool;
}

QColor CanvasWidget::brushColor() const
{
    return m_brushColor;
}

qreal CanvasWidget::brushWidth() const
{
    return m_brushWidth;
}

bool CanvasWidget::isAnimating() const
{
    return m_animating;
}

int CanvasWidget::currentFrame() const
{
    return m_currentFrame;
}

qreal CanvasWidget::zoom() const
{
    return documentTransform().m11();
}

void CanvasWidget::setTool(Tool tool)
{
    if (m_tool == tool) {
        return;
    }
    cancelStroke();
    cancelLasso();
    clearSelection();
    m_tool = tool;
    emit toolChanged(tool);
    updateCursor();
    update();
}

void CanvasWidget::setBrushColor(const QColor &color)
{
    if (!color.isValid() || m_brushColor == color) {
        return;
    }
    m_brushColor = color;
    emit brushColorChanged(color);
    update();
}

void CanvasWidget::setBrushWidth(qreal width)
{
    const qreal normalized = std::clamp(
        width,
        DocumentLimits::minimumStrokeWidth,
        DocumentLimits::maximumStrokeWidth);
    if (qFuzzyCompare(m_brushWidth, normalized)) {
        return;
    }
    m_brushWidth = normalized;
    emit brushWidthChanged(normalized);
    update();
}

void CanvasWidget::setAnimating(bool animating)
{
    if (m_animating == animating) {
        return;
    }
    m_animating = animating;
    if (m_animating) {
        updateTimerInterval();
        m_animationTimer.start();
    } else {
        m_animationTimer.stop();
    }
    emit animatingChanged(animating);
    update();
}

void CanvasWidget::toggleAnimating()
{
    setAnimating(!m_animating);
}

void CanvasWidget::setAnimateWhileDrawing(bool animate)
{
    m_animateWhileDrawing = animate;
}

void CanvasWidget::fitToWindow()
{
    m_zoom = 1.0;
    m_pan = QPointF();
    notifyZoomChanged();
    update();
}

void CanvasWidget::setCurrentFrame(int frame)
{
    const int frameCount =
        std::max(1, m_controller->document().animationFrames);
    const int normalized = ((frame % frameCount) + frameCount) % frameCount;
    if (normalized == m_currentFrame) {
        return;
    }
    m_currentFrame = normalized;
    emit currentFrameChanged(normalized);
    update();
}

void CanvasWidget::setPanModifierActive(bool active)
{
    if (m_spacePressed == active) {
        return;
    }
    m_spacePressed = active;
    updateCursor();
    update();
}

void CanvasWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), Theme::canvasBackground());
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    const Document &document = m_controller->document();
    const QTransform transform = documentTransform();
    const QRectF canvasRect =
        transform.mapRect(QRectF(QPointF(0.0, 0.0), QSizeF(document.size)));

    painter.save();
    painter.setClipRect(canvasRect);
    const int checkerSize = 12;
    const int left = static_cast<int>(std::floor(canvasRect.left() / checkerSize));
    const int top = static_cast<int>(std::floor(canvasRect.top() / checkerSize));
    const int right = static_cast<int>(std::ceil(canvasRect.right() / checkerSize));
    const int bottom = static_cast<int>(std::ceil(canvasRect.bottom() / checkerSize));
    for (int y = top; y <= bottom; ++y) {
        for (int x = left; x <= right; ++x) {
            painter.fillRect(
                QRectF(
                    x * checkerSize,
                    y * checkerSize,
                    checkerSize,
                    checkerSize),
                (x + y) % 2 == 0
                    ? QColor(238, 238, 238)
                    : QColor(210, 210, 210));
        }
    }
    painter.restore();

    painter.save();
    painter.setTransform(transform);
    QImage displayedFrame;
    if (m_drawing && !m_activeStroke.points.isEmpty()) {
        Document previewDocument = document;
        if (Layer *layer = previewDocument.layer(m_activeStrokeLayer)) {
            layer->strokes.append(m_activeStroke);
            displayedFrame =
                RenderEngine::render(previewDocument, m_currentFrame);
        }
    } else if (m_movingSelection
               && !m_selectedStrokes.isEmpty()
               && (!qFuzzyIsNull(m_moveDelta.x())
                   || !qFuzzyIsNull(m_moveDelta.y()))) {
        Document previewDocument = document;
        if (Layer *layer = previewDocument.layer(m_selectionLayer)) {
            for (Stroke &stroke : layer->strokes) {
                if (m_selectedStrokes.contains(stroke.id)) {
                    for (StrokePoint &point : stroke.points) {
                        point.position += m_moveDelta;
                    }
                }
            }
            displayedFrame =
                RenderEngine::render(previewDocument, m_currentFrame);
        }
    }
    if (displayedFrame.isNull()) {
        displayedFrame = frameImage(m_currentFrame);
    }
    painter.drawImage(QPointF(0.0, 0.0), displayedFrame);
    painter.restore();

    painter.setPen(QPen(Theme::canvasBorder(), 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(canvasRect);

    if (!m_drawing && !documentHasStrokes(document)) {
        painter.setPen(QColor(0x9A, 0x9E, 0xA6));
        QFont hintFont = painter.font();
        hintFont.setPointSizeF(hintFont.pointSizeF() * 0.95);
        painter.setFont(hintFont);
        painter.drawText(
            canvasRect.adjusted(0.0, 0.0, 0.0, -14.0),
            Qt::AlignHCenter | Qt::AlignBottom,
            tr("B Brush · E Eraser · Space Pan · Scroll Zoom · P Play"));
    }

    drawSelectionOverlay(painter, transform);

    if (m_pointerOverWidget
        && !m_panning
        && !m_spacePressed
        && (m_tool == Tool::Brush || m_tool == Tool::Eraser)) {
        const qreal radius = std::max(
            1.0,
            m_brushWidth * std::abs(transform.m11()) * 0.5);
        const QRectF footprint(
            m_pointerWidgetPosition.x() - radius,
            m_pointerWidgetPosition.y() - radius,
            radius * 2.0,
            radius * 2.0);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(20, 20, 20, 220), 3.0));
        painter.drawEllipse(footprint);
        QPen innerPen(QColor(250, 250, 250, 235), 1.0);
        if (m_tool == Tool::Eraser) {
            innerPen.setStyle(Qt::DashLine);
        }
        painter.setPen(innerPen);
        painter.drawEllipse(footprint);
    }
}

void CanvasWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    notifyZoomChanged();
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
    updatePointerPosition(event->position());
    setFocus(Qt::MouseFocusReason);
    if (event->button() == Qt::MiddleButton
        || (event->button() == Qt::LeftButton && m_spacePressed)) {
        beginPan(event->position());
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && !m_tabletSequence) {
        const QPointF documentPosition = mapToDocument(event->position());
        switch (m_tool) {
        case Tool::Brush:
        case Tool::Eraser:
            beginStroke(event->position(), 1.0, false);
            break;
        case Tool::Lasso:
            if (selectionContains(documentPosition)) {
                beginSelectionMove(documentPosition);
            } else {
                beginLasso(documentPosition);
            }
            break;
        case Tool::Wand:
            if (selectionContains(documentPosition)) {
                beginSelectionMove(documentPosition);
            } else {
                computeWandSelection(documentPosition);
            }
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
    updatePointerPosition(event->position());
    if (m_panning) {
        continuePan(event->position());
        event->accept();
        return;
    }
    if (m_drawing && !m_tabletSequence) {
        continueStroke(event->position(), 1.0);
        event->accept();
        return;
    }
    if (m_movingSelection) {
        continueSelectionMove(mapToDocument(event->position()));
        event->accept();
        return;
    }
    if (m_lassoActive) {
        const QPointF position =
            clampedDocumentPosition(mapToDocument(event->position()));
        if (m_lassoPoints.isEmpty()
            || pointDistance(position, m_lassoPoints.constLast()) >= 1.0) {
            m_lassoPoints.append(position);
            update();
        }
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void CanvasWidget::mouseReleaseEvent(QMouseEvent *event)
{
    updatePointerPosition(event->position());
    if (m_panning
        && (event->button() == Qt::MiddleButton
            || event->button() == Qt::LeftButton)) {
        endPan();
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton
        && m_drawing
        && !m_tabletSequence) {
        endStroke(event->position(), 1.0);
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && m_movingSelection) {
        continueSelectionMove(mapToDocument(event->position()));
        commitSelectionMove();
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && m_lassoActive) {
        finishLasso();
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void CanvasWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MiddleButton) {
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
    bool inside = false;
    const QPointF anchor = mapToDocument(cursor, &inside);
    const qreal factor = std::pow(1.0015, event->angleDelta().y());
    const qreal previousZoom = m_zoom;
    m_zoom = std::clamp(m_zoom * factor, 0.1, 12.0);
    if (qFuzzyCompare(previousZoom, m_zoom)) {
        return;
    }
    if (inside) {
        const QPointF mappedAfter = documentTransform().map(anchor);
        m_pan += cursor - mappedAfter;
    }
    notifyZoomChanged();
    update();
    event->accept();
}

void CanvasWidget::tabletEvent(QTabletEvent *event)
{
    const bool eraser =
        event->pointerType() == QPointingDevice::PointerType::Eraser;
    if (!eraser && m_tool != Tool::Brush && m_tool != Tool::Eraser) {
        QWidget::tabletEvent(event);
        return;
    }
    if (event->type() == QEvent::TabletPress) {
        m_tabletSequence = true;
        beginStroke(event->position(), event->pressure(), eraser);
        event->accept();
        return;
    }
    if (event->type() == QEvent::TabletMove && m_tabletSequence) {
        updatePointerPosition(event->position());
        continueStroke(event->position(), event->pressure());
        event->accept();
        return;
    }
    if (event->type() == QEvent::TabletRelease && m_tabletSequence) {
        endStroke(event->position(), event->pressure());
        m_tabletSequence = false;
        event->accept();
        return;
    }
    QWidget::tabletEvent(event);
}

void CanvasWidget::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        setPanModifierActive(true);
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape) {
        cancelStroke();
        cancelLasso();
        clearSelection();
        endPan();
        event->accept();
        return;
    }
    if ((event->key() == Qt::Key_Delete
         || event->key() == Qt::Key_Backspace)
        && !m_selectedStrokes.isEmpty()) {
        m_controller->removeStrokes(
            m_selectionLayer,
            QVector<QUuid>(
                m_selectedStrokes.cbegin(),
                m_selectedStrokes.cend()));
        clearSelection();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void CanvasWidget::keyReleaseEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        setPanModifierActive(false);
        event->accept();
        return;
    }
    QWidget::keyReleaseEvent(event);
}

void CanvasWidget::leaveEvent(QEvent *event)
{
    m_pointerInside = false;
    m_pointerOverWidget = false;
    emit pointerPositionChanged(QPointF(), false);
    update();
    QWidget::leaveEvent(event);
}

QTransform CanvasWidget::documentTransform() const
{
    const QSize canvasSize = m_controller->document().size;
    if (!canvasSize.isValid()) {
        return {};
    }
    const qreal availableWidth = std::max(1.0, width() - canvasMargin * 2.0);
    const qreal availableHeight = std::max(1.0, height() - canvasMargin * 2.0);
    const qreal baseScale = std::min(
        availableWidth / canvasSize.width(),
        availableHeight / canvasSize.height());
    const qreal scale = baseScale * m_zoom;
    const QPointF center(width() * 0.5, height() * 0.5);
    const QPointF canvasCenter(
        canvasSize.width() * 0.5,
        canvasSize.height() * 0.5);

    QTransform transform;
    transform.translate(center.x() + m_pan.x(), center.y() + m_pan.y());
    transform.scale(scale, scale);
    transform.translate(-canvasCenter.x(), -canvasCenter.y());
    return transform;
}

QPointF CanvasWidget::mapToDocument(
    const QPointF &widgetPosition,
    bool *inside) const
{
    bool invertible = false;
    const QTransform inverse = documentTransform().inverted(&invertible);
    const QPointF position =
        invertible ? inverse.map(widgetPosition) : QPointF();
    const QRectF bounds(
        QPointF(0.0, 0.0),
        QSizeF(m_controller->document().size));
    if (inside) {
        *inside = invertible && bounds.contains(position);
    }
    return position;
}

QPointF CanvasWidget::clampedDocumentPosition(const QPointF &position) const
{
    const QSize size = m_controller->document().size;
    return QPointF(
        std::clamp(position.x(), 0.0, static_cast<qreal>(size.width())),
        std::clamp(position.y(), 0.0, static_cast<qreal>(size.height())));
}

QImage CanvasWidget::frameImage(int frame)
{
    if (QImage *cached = m_frameCache.object(frame)) {
        return *cached;
    }
    QImage image = RenderEngine::render(m_controller->document(), frame);
    if (image.isNull()) {
        return {};
    }
    const qsizetype bytes = image.sizeInBytes();
    const int cost = static_cast<int>(
        std::clamp<qsizetype>(bytes / 1024, 1, std::numeric_limits<int>::max()));
    m_frameCache.insert(frame, new QImage(image), cost);
    return image;
}

void CanvasWidget::invalidateFrames()
{
    m_frameCache.clear();
    const int frames = std::max(1, m_controller->document().animationFrames);
    m_currentFrame %= frames;
    updateTimerInterval();
    notifyZoomChanged();
    update();
}

void CanvasWidget::updateTimerInterval()
{
    const qreal fps = std::clamp(
        m_controller->document().framesPerSecond,
        DocumentLimits::minimumFramesPerSecond,
        DocumentLimits::maximumFramesPerSecond);
    m_animationTimer.setInterval(std::max(1, qRound(1000.0 / fps)));
}

void CanvasWidget::advanceFrame()
{
    if (!m_animating || (m_drawing && !m_animateWhileDrawing)) {
        return;
    }
    setCurrentFrame(m_currentFrame + 1);
}

void CanvasWidget::beginStroke(
    const QPointF &widgetPosition,
    qreal pressure,
    bool tabletEraser)
{
    bool inside = false;
    const QPointF documentPosition =
        mapToDocument(widgetPosition, &inside);
    const Document &document = m_controller->document();
    const Layer *layer = document.layer(document.activeLayerId);
    if (!inside || !layer) {
        return;
    }
    if (!layer->visible) {
        emit interactionMessage(
            tr("The active layer is hidden. Make it visible to draw."));
        return;
    }
    if (layer->opacity <= 0.0) {
        emit interactionMessage(
            tr("The active layer opacity is 0%. Increase it to draw."));
        return;
    }

    m_activeStroke = Stroke();
    m_activeStroke.seed = QRandomGenerator::global()->generate64();
    m_activeStroke.mode =
        tabletEraser || m_tool == Tool::Eraser
        ? StrokeMode::Erase
        : StrokeMode::Paint;
    m_activeStroke.color = m_brushColor;
    m_activeStroke.width = m_brushWidth;
    m_activeStroke.points.append({
        clampedDocumentPosition(documentPosition),
        std::clamp(pressure, 0.05, 1.0)
    });
    m_activeStrokeLayer = document.activeLayerId;
    m_drawing = true;
    update();
}

void CanvasWidget::continueStroke(
    const QPointF &widgetPosition,
    qreal pressure)
{
    if (!m_drawing) {
        return;
    }
    if (m_activeStroke.points.size()
        >= DocumentLimits::maximumPointsPerStroke) {
        return;
    }
    const QPointF position =
        clampedDocumentPosition(mapToDocument(widgetPosition));
    if (pointDistance(position, m_activeStroke.points.constLast().position) < 0.75) {
        return;
    }
    m_activeStroke.points.append({
        position,
        std::clamp(pressure, 0.05, 1.0)
    });
    update();
}

void CanvasWidget::endStroke(
    const QPointF &widgetPosition,
    qreal pressure)
{
    if (!m_drawing) {
        return;
    }
    continueStroke(widgetPosition, pressure);
    Stroke completed = m_activeStroke;
    const QUuid layerId = m_activeStrokeLayer;
    m_drawing = false;
    m_activeStroke = Stroke();
    m_activeStrokeLayer = {};
    m_controller->addStroke(layerId, std::move(completed));
    update();
}

void CanvasWidget::cancelStroke()
{
    if (!m_drawing) {
        return;
    }
    m_drawing = false;
    m_activeStroke = Stroke();
    m_activeStrokeLayer = {};
    update();
}

void CanvasWidget::beginPan(const QPointF &widgetPosition)
{
    cancelStroke();
    m_panning = true;
    m_lastPanPosition = widgetPosition;
    updateCursor();
}

void CanvasWidget::continuePan(const QPointF &widgetPosition)
{
    m_pan += widgetPosition - m_lastPanPosition;
    m_lastPanPosition = widgetPosition;
    update();
}

void CanvasWidget::endPan()
{
    if (!m_panning) {
        return;
    }
    m_panning = false;
    updateCursor();
}

void CanvasWidget::updatePointerPosition(const QPointF &widgetPosition)
{
    bool inside = false;
    const QPointF position = mapToDocument(widgetPosition, &inside);
    m_pointerWidgetPosition = widgetPosition;
    m_pointerInside = inside;
    m_pointerOverWidget = true;
    emit pointerPositionChanged(position, inside);
    update();
}

void CanvasWidget::updateCursor()
{
    if (m_panning) {
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (m_spacePressed) {
        setCursor(Qt::OpenHandCursor);
        return;
    }
    const bool drawsWithRing =
        m_tool == Tool::Brush || m_tool == Tool::Eraser;
    setCursor(drawsWithRing ? Qt::BlankCursor : Qt::CrossCursor);
}

void CanvasWidget::notifyZoomChanged()
{
    emit zoomChanged(std::max(1, qRound(std::abs(zoom()) * 100.0)));
}

bool CanvasWidget::selectionContains(const QPointF &documentPosition) const
{
    if (m_selectionMask.isNull()) {
        return false;
    }
    const QPoint pixel(
        static_cast<int>(documentPosition.x()),
        static_cast<int>(documentPosition.y()));
    return m_selectionMask.rect().contains(pixel)
        && m_selectionMask.constScanLine(pixel.y())[pixel.x()] >= 128;
}

void CanvasWidget::beginLasso(const QPointF &documentPosition)
{
    clearSelection();
    m_lassoActive = true;
    m_lassoPoints.clear();
    m_lassoPoints.append(clampedDocumentPosition(documentPosition));
    update();
}

void CanvasWidget::finishLasso()
{
    if (!m_lassoActive) {
        return;
    }
    const QVector<QPointF> points = m_lassoPoints;
    cancelLasso();
    if (points.size() < 3) {
        return;
    }

    const QSize size = m_controller->document().size;
    QImage mask(size, QImage::Format_Grayscale8);
    mask.fill(0);
    QPainter painter(&mask);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::white);
    painter.drawPolygon(QPolygonF(points));
    painter.end();
    applySelectionMask(mask);
}

void CanvasWidget::cancelLasso()
{
    if (!m_lassoActive && m_lassoPoints.isEmpty()) {
        return;
    }
    m_lassoActive = false;
    m_lassoPoints.clear();
    update();
}

void CanvasWidget::applySelectionMask(QImage mask)
{
    const Document &document = m_controller->document();
    const Layer *layer = document.layer(document.activeLayerId);
    if (mask.isNull() || !layer) {
        clearSelection();
        return;
    }

    QSet<QUuid> selected;
    for (const Stroke &stroke : layer->strokes) {
        const bool inside = std::any_of(
            stroke.points.cbegin(),
            stroke.points.cend(),
            [&mask](const StrokePoint &point) {
                const QPoint pixel(
                    static_cast<int>(point.position.x()),
                    static_cast<int>(point.position.y()));
                return mask.rect().contains(pixel)
                    && mask.constScanLine(pixel.y())[pixel.x()] >= 128;
            });
        if (inside) {
            selected.insert(stroke.id);
        }
    }

    m_selectionMask = mask;
    m_selectedStrokes = selected;
    m_selectionLayer = document.activeLayerId;
    m_moveDelta = QPointF();

    m_selectionTint = QImage(
        mask.size(),
        QImage::Format_ARGB32_Premultiplied);
    m_selectionTint.fill(Qt::transparent);
    QColor tint = Theme::accent();
    tint.setAlpha(56);
    const QRgb tintPixel = qPremultiply(tint.rgba());
    for (int y = 0; y < mask.height(); ++y) {
        const uchar *maskLine = mask.constScanLine(y);
        QRgb *tintLine =
            reinterpret_cast<QRgb *>(m_selectionTint.scanLine(y));
        for (int x = 0; x < mask.width(); ++x) {
            if (maskLine[x] >= 128) {
                tintLine[x] = tintPixel;
            }
        }
    }

    emit interactionMessage(
        selected.isEmpty()
            ? tr("No strokes in the selected area.")
            : tr("%n stroke(s) selected. Drag to move, press Delete to "
                 "remove.",
                 nullptr,
                 static_cast<int>(selected.size())));
    update();
}

void CanvasWidget::computeWandSelection(const QPointF &documentPosition)
{
    clearSelection();
    const QSize size = m_controller->document().size;
    const QRectF bounds(QPointF(0.0, 0.0), QSizeF(size));
    if (!bounds.contains(documentPosition)) {
        return;
    }
    const QPoint seed(
        std::clamp(static_cast<int>(documentPosition.x()), 0, size.width() - 1),
        std::clamp(
            static_cast<int>(documentPosition.y()),
            0,
            size.height() - 1));

    const QImage layerImage = renderActiveLayerImage();
    if (layerImage.isNull()) {
        return;
    }
    const QImage mask = RenderEngine::fillRegionMask(layerImage, seed);
    if (mask.isNull()) {
        emit interactionMessage(
            tr("Click an empty area surrounded by lines to select it."));
        return;
    }
    applySelectionMask(mask);
}

void CanvasWidget::applyBucketFill(const QPointF &documentPosition)
{
    const Document &document = m_controller->document();
    const QRectF bounds(QPointF(0.0, 0.0), QSizeF(document.size));
    const Layer *layer = document.layer(document.activeLayerId);
    if (!layer || !bounds.contains(documentPosition)) {
        return;
    }
    if (!layer->visible) {
        emit interactionMessage(
            tr("The active layer is hidden. Make it visible to draw."));
        return;
    }
    if (layer->opacity <= 0.0) {
        emit interactionMessage(
            tr("The active layer opacity is 0%. Increase it to draw."));
        return;
    }

    Stroke fillStroke;
    fillStroke.seed = QRandomGenerator::global()->generate64();
    fillStroke.mode = StrokeMode::Fill;
    fillStroke.color = m_brushColor;
    fillStroke.width = std::clamp(
        m_brushWidth,
        DocumentLimits::minimumStrokeWidth,
        DocumentLimits::maximumStrokeWidth);
    fillStroke.points.append({
        clampedDocumentPosition(documentPosition),
        1.0
    });
    m_controller->addStroke(document.activeLayerId, std::move(fillStroke));
}

void CanvasWidget::beginSelectionMove(const QPointF &documentPosition)
{
    m_movingSelection = true;
    m_moveStartPosition = documentPosition;
    m_moveDelta = QPointF();
}

void CanvasWidget::continueSelectionMove(const QPointF &documentPosition)
{
    if (!m_movingSelection) {
        return;
    }
    m_moveDelta =
        clampedSelectionDelta(documentPosition - m_moveStartPosition);
    update();
}

void CanvasWidget::commitSelectionMove()
{
    if (!m_movingSelection) {
        return;
    }
    m_movingSelection = false;
    const QPointF delta = m_moveDelta;
    m_moveDelta = QPointF();
    if (qFuzzyIsNull(delta.x()) && qFuzzyIsNull(delta.y())) {
        update();
        return;
    }

    if (!m_selectedStrokes.isEmpty()) {
        m_controller->translateStrokes(
            m_selectionLayer,
            QVector<QUuid>(
                m_selectedStrokes.cbegin(),
                m_selectedStrokes.cend()),
            delta);
    }

    const QPoint shift(qRound(delta.x()), qRound(delta.y()));
    if (!m_selectionMask.isNull() && !shift.isNull()) {
        QImage movedMask(m_selectionMask.size(), m_selectionMask.format());
        movedMask.fill(0);
        QPainter maskPainter(&movedMask);
        maskPainter.drawImage(shift, m_selectionMask);
        maskPainter.end();
        m_selectionMask = movedMask;

        QImage movedTint(m_selectionTint.size(), m_selectionTint.format());
        movedTint.fill(Qt::transparent);
        QPainter tintPainter(&movedTint);
        tintPainter.drawImage(shift, m_selectionTint);
        tintPainter.end();
        m_selectionTint = movedTint;
    }
    update();
}

void CanvasWidget::clearSelection()
{
    if (m_selectedStrokes.isEmpty()
        && m_selectionMask.isNull()
        && !m_movingSelection) {
        return;
    }
    m_selectedStrokes.clear();
    m_selectionMask = QImage();
    m_selectionTint = QImage();
    m_selectionLayer = {};
    m_movingSelection = false;
    m_moveDelta = QPointF();
    update();
}

void CanvasWidget::pruneSelection()
{
    if (m_selectedStrokes.isEmpty() && m_selectionMask.isNull()) {
        return;
    }
    const Document &document = m_controller->document();
    const Layer *layer = document.layer(m_selectionLayer);
    if (!layer || document.size != m_selectionMask.size()) {
        clearSelection();
        return;
    }
    QSet<QUuid> remaining;
    for (const Stroke &stroke : layer->strokes) {
        if (m_selectedStrokes.contains(stroke.id)) {
            remaining.insert(stroke.id);
        }
    }
    if (remaining.size() != m_selectedStrokes.size()) {
        m_selectedStrokes = remaining;
        update();
    }
}

QPointF CanvasWidget::clampedSelectionDelta(const QPointF &delta) const
{
    const Document &document = m_controller->document();
    const Layer *layer = document.layer(m_selectionLayer);
    if (!layer || m_selectedStrokes.isEmpty()) {
        return delta;
    }
    qreal minX = document.size.width();
    qreal minY = document.size.height();
    qreal maxX = 0.0;
    qreal maxY = 0.0;
    bool found = false;
    for (const Stroke &stroke : layer->strokes) {
        if (!m_selectedStrokes.contains(stroke.id)) {
            continue;
        }
        for (const StrokePoint &point : stroke.points) {
            minX = std::min(minX, point.position.x());
            minY = std::min(minY, point.position.y());
            maxX = std::max(maxX, point.position.x());
            maxY = std::max(maxY, point.position.y());
            found = true;
        }
    }
    if (!found) {
        return delta;
    }
    return QPointF(
        std::clamp(
            delta.x(),
            -minX,
            static_cast<qreal>(document.size.width()) - maxX),
        std::clamp(
            delta.y(),
            -minY,
            static_cast<qreal>(document.size.height()) - maxY));
}

QRectF CanvasWidget::selectionBounds() const
{
    const Document &document = m_controller->document();
    const Layer *layer = document.layer(m_selectionLayer);
    if (!layer || m_selectedStrokes.isEmpty()) {
        return {};
    }
    QRectF bounds;
    for (const Stroke &stroke : layer->strokes) {
        if (!m_selectedStrokes.contains(stroke.id)) {
            continue;
        }
        for (const StrokePoint &point : stroke.points) {
            const QRectF pointRect(point.position, QSizeF(0.0, 0.0));
            bounds = bounds.isNull() ? pointRect : bounds.united(pointRect);
        }
    }
    return bounds.translated(m_moveDelta);
}

QImage CanvasWidget::renderActiveLayerImage() const
{
    const Document &document = m_controller->document();
    const Layer *layer = document.layer(document.activeLayerId);
    if (!layer) {
        return {};
    }
    Document single = document;
    single.background = Qt::transparent;
    Layer visibleLayer = *layer;
    visibleLayer.visible = true;
    visibleLayer.opacity = 1.0;
    single.layers = {visibleLayer};
    return RenderEngine::render(single, m_currentFrame);
}

void CanvasWidget::drawSelectionOverlay(
    QPainter &painter,
    const QTransform &transform)
{
    if (m_lassoActive && m_lassoPoints.size() >= 2) {
        QPen lassoPen(Theme::accent(), 1.4);
        lassoPen.setStyle(Qt::DashLine);
        lassoPen.setCosmetic(true);
        painter.setPen(lassoPen);
        painter.setBrush(Qt::NoBrush);
        painter.drawPolyline(transform.map(QPolygonF(m_lassoPoints)));
    }

    if (m_selectionTint.isNull()) {
        return;
    }
    painter.save();
    painter.setTransform(transform);
    painter.drawImage(m_moveDelta, m_selectionTint);
    painter.restore();

    const QRectF bounds = selectionBounds();
    if (!bounds.isNull()) {
        QPen boundsPen(Theme::accent(), 1.0);
        boundsPen.setStyle(Qt::DashLine);
        boundsPen.setCosmetic(true);
        painter.setPen(boundsPen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(
            transform.mapRect(bounds.adjusted(-4.0, -4.0, 4.0, 4.0)));
    }
}

}
