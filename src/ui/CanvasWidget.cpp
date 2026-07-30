#include "ui/CanvasWidget.hpp"

#include "document/DocumentLimits.hpp"
#include "render/RenderEngine.hpp"
#include "ui/Theme.hpp"

#include <QEnterEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
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

    if (m_pointerOverWidget && !m_panning && !m_spacePressed) {
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
        beginStroke(event->position(), 1.0, false);
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
        endPan();
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
    setCursor(m_spacePressed ? Qt::OpenHandCursor : Qt::BlankCursor);
}

void CanvasWidget::notifyZoomChanged()
{
    emit zoomChanged(std::max(1, qRound(std::abs(zoom()) * 100.0)));
}

}
