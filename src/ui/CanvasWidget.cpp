#include "ui/CanvasWidget.hpp"

#include "brush/BrushPreset.hpp"
#include "document/DocumentLimits.hpp"
#include "render/RenderEngine.hpp"
#include "ui/Theme.hpp"

#include <QEnterEvent>
#include <QHash>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPathStroker>
#include <QPointer>
#include <QPolygonF>
#include <QRandomGenerator>
#include <QResizeEvent>
#include <QTabletEvent>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <cstring>
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

quint64 encodedPoint(int x, int y)
{
    return (static_cast<quint64>(static_cast<quint32>(x)) << 32U)
        | static_cast<quint32>(y);
}

QPointF decodedPoint(quint64 point)
{
    return QPointF(
        static_cast<quint32>(point >> 32U),
        static_cast<quint32>(point));
}

QPainterPath outlinePath(const QImage &mask)
{
    QHash<quint64, QVector<quint64>> edges;
    const auto inside = [&mask](int x, int y) {
        return x >= 0
            && y >= 0
            && x < mask.width()
            && y < mask.height()
            && mask.constScanLine(y)[x] >= 128;
    };
    const auto addEdge = [&edges](int x1, int y1, int x2, int y2) {
        edges[encodedPoint(x1, y1)].append(encodedPoint(x2, y2));
    };

    for (int y = 0; y < mask.height(); ++y) {
        const uchar *line = mask.constScanLine(y);
        for (int x = 0; x < mask.width(); ++x) {
            if (line[x] < 128) {
                continue;
            }
            if (!inside(x, y - 1)) {
                addEdge(x, y, x + 1, y);
            }
            if (!inside(x + 1, y)) {
                addEdge(x + 1, y, x + 1, y + 1);
            }
            if (!inside(x, y + 1)) {
                addEdge(x + 1, y + 1, x, y + 1);
            }
            if (!inside(x - 1, y)) {
                addEdge(x, y + 1, x, y);
            }
        }
    }

    QPainterPath path;
    while (!edges.isEmpty()) {
        auto first = edges.begin();
        const quint64 start = first.key();
        quint64 current = start;
        path.moveTo(decodedPoint(start));

        do {
            auto edge = edges.find(current);
            if (edge == edges.end()) {
                break;
            }
            const quint64 next = edge.value().takeLast();
            if (edge.value().isEmpty()) {
                edges.erase(edge);
            }
            path.lineTo(decodedPoint(next));
            current = next;
        } while (current != start);

        if (current == start) {
            path.closeSubpath();
        }
    }
    return path;
}

void drawSelectionPath(
    QPainter &painter,
    const QPainterPath &path,
    qreal dashOffset)
{
    QPen lightPen(QColor(255, 255, 255, 235), 1.8);
    lightPen.setCosmetic(true);
    lightPen.setJoinStyle(Qt::MiterJoin);
    painter.setPen(lightPen);
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(path);

    QPen darkPen(QColor(20, 20, 20, 245), 1.0);
    darkPen.setCosmetic(true);
    darkPen.setJoinStyle(Qt::MiterJoin);
    darkPen.setDashPattern({4.0, 4.0});
    darkPen.setDashOffset(dashOffset);
    painter.setPen(darkPen);
    painter.drawPath(path);
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
    const BrushPreset &defaultPreset = BrushPresetCatalog::defaultPreset();
    m_brushPresetId = defaultPreset.id;
    m_brushSettings = defaultPreset.settings;
    m_brushWidth = defaultPreset.defaultSize;
    m_presetWidths.insert(m_brushPresetId, m_brushWidth);

    connect(m_controller, &DocumentController::documentChanged, this, [this]() {
        invalidateFrames();
        pruneSelection();
    });
    connect(
        m_controller,
        &DocumentController::strokesTranslated,
        this,
        &CanvasWidget::translateSelectionOverlay);
    connect(&m_animationTimer, &QTimer::timeout, this, [this]() {
        advanceFrame();
    });
    m_selectionAnimationTimer.setInterval(120);
    connect(&m_selectionAnimationTimer, &QTimer::timeout, this, [this]() {
        m_selectionDashOffset -= 1.0;
        update();
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

QString CanvasWidget::brushPresetId() const
{
    return m_brushPresetId;
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

bool CanvasWidget::hasSelection() const
{
    return !m_selectionMask.isNull();
}

void CanvasWidget::setTool(Tool tool)
{
    if (m_tool == tool) {
        return;
    }
    const SelectionState previousSelection =
        m_lassoActive && m_hasSelectionBeforeLasso
        ? m_selectionBeforeLasso
        : currentSelectionState();
    cancelStroke();
    cancelLasso();
    pushSelectionChange(previousSelection, {}, tr("Deselect"));
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
    if (!m_brushPresetId.isEmpty()) {
        m_presetWidths.insert(m_brushPresetId, normalized);
    }
    emit brushWidthChanged(normalized);
    update();
}

void CanvasWidget::setBrushPreset(const QString &presetId)
{
    const BrushPreset *preset = BrushPresetCatalog::find(presetId);
    if (!preset || m_brushPresetId == preset->id) {
        return;
    }
    cancelStroke();
    m_brushPresetId = preset->id;
    m_brushSettings = preset->settings;
    const qreal nextWidth = std::clamp(
        m_presetWidths.value(preset->id, preset->defaultSize),
        DocumentLimits::minimumStrokeWidth,
        DocumentLimits::maximumStrokeWidth);
    m_presetWidths.insert(preset->id, nextWidth);
    if (!qFuzzyCompare(m_brushWidth, nextWidth)) {
        m_brushWidth = nextWidth;
        emit brushWidthChanged(nextWidth);
    }
    emit brushPresetChanged(preset->id);
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
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);

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
    const QSize renderSize = previewRenderSize();
    QImage displayedFrame;
    if (m_drawing && !m_activeStroke.points.isEmpty()) {
        Document previewDocument = document;
        if (Layer *layer = previewDocument.layer(m_activeStrokeLayer)) {
            layer->strokes.append(m_activeStroke);
            displayedFrame = RenderEngine::renderScaled(
                previewDocument,
                m_currentFrame,
                renderSize);
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
            displayedFrame = RenderEngine::renderScaled(
                previewDocument,
                m_currentFrame,
                renderSize);
        }
    }
    if (displayedFrame.isNull()) {
        displayedFrame = frameImage(m_currentFrame);
    }
    painter.drawImage(
        QRectF(QPointF(0.0, 0.0), QSizeF(document.size)),
        displayedFrame);
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
    if (event->type() == QEvent::TabletPress) {
        if (event->button() != Qt::LeftButton) {
            QWidget::tabletEvent(event);
            return;
        }
        if (m_spacePressed) {
            m_tabletSequence = true;
            beginPan(event->position());
            event->accept();
            return;
        }
        if (!eraser && m_tool != Tool::Brush && m_tool != Tool::Eraser) {
            QWidget::tabletEvent(event);
            return;
        }
        m_tabletSequence = true;
        beginStroke(event->position(), event->pressure(), eraser);
        event->accept();
        return;
    }
    if (event->type() == QEvent::TabletMove && m_tabletSequence) {
        updatePointerPosition(event->position());
        if (m_panning) {
            continuePan(event->position());
        } else {
            continueStroke(event->position(), event->pressure());
        }
        event->accept();
        return;
    }
    if (event->type() == QEvent::TabletRelease && m_tabletSequence) {
        if (m_panning) {
            endPan();
        } else {
            endStroke(event->position(), event->pressure());
        }
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
        const SelectionState previousSelection =
            m_lassoActive && m_hasSelectionBeforeLasso
            ? m_selectionBeforeLasso
            : currentSelectionState();
        cancelStroke();
        cancelLasso();
        pushSelectionChange(previousSelection, {}, tr("Deselect"));
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
    const QSize renderSize = previewRenderSize();
    if (renderSize != m_cachedRenderSize) {
        m_frameCache.clear();
        m_cachedRenderSize = renderSize;
    }
    if (QImage *cached = m_frameCache.object(frame)) {
        return *cached;
    }
    QImage image = RenderEngine::renderScaled(
        m_controller->document(),
        frame,
        renderSize);
    if (image.isNull()) {
        return {};
    }
    const qsizetype bytes = image.sizeInBytes();
    const int cost = static_cast<int>(
        std::clamp<qsizetype>(bytes / 1024, 1, std::numeric_limits<int>::max()));
    m_frameCache.insert(frame, new QImage(image), cost);
    return image;
}

QSize CanvasWidget::previewRenderSize() const
{
    const QSize documentSize = m_controller->document().size;
    if (!documentSize.isValid()) {
        return {};
    }
    constexpr qreal maximumPreviewEdge = 4096.0;
    const qreal displayScale =
        std::abs(documentTransform().m11()) * devicePixelRatioF();
    const qreal edgeScale = std::min(
        maximumPreviewEdge / documentSize.width(),
        maximumPreviewEdge / documentSize.height());
    const qreal scale = std::clamp(
        std::min(displayScale, edgeScale),
        1.0 / std::max(documentSize.width(), documentSize.height()),
        1.0);
    return QSize(
        std::max(1, qCeil(documentSize.width() * scale)),
        std::max(1, qCeil(documentSize.height() * scale)));
}

void CanvasWidget::invalidateFrames()
{
    m_frameCache.clear();
    m_cachedRenderSize = {};
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
    m_activeStroke.brush = m_brushSettings;
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
    m_selectionBeforeLasso = currentSelectionState();
    m_hasSelectionBeforeLasso = true;
    clearSelection();
    m_lassoActive = true;
    m_lassoPoints.clear();
    m_lassoPoints.append(clampedDocumentPosition(documentPosition));
    updateSelectionAnimation();
    update();
}

void CanvasWidget::finishLasso()
{
    if (!m_lassoActive) {
        return;
    }
    const QVector<QPointF> points = m_lassoPoints;
    const SelectionState previousSelection = m_hasSelectionBeforeLasso
        ? m_selectionBeforeLasso
        : SelectionState();
    m_lassoActive = false;
    m_lassoPoints.clear();
    m_selectionBeforeLasso = {};
    m_hasSelectionBeforeLasso = false;
    if (points.size() < 3) {
        pushSelectionChange(
            previousSelection,
            {},
            tr("Deselect"));
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
    applySelectionMask(std::move(mask), previousSelection);
}

void CanvasWidget::cancelLasso()
{
    if (!m_lassoActive && m_lassoPoints.isEmpty()) {
        return;
    }
    m_lassoActive = false;
    m_lassoPoints.clear();
    m_selectionBeforeLasso = {};
    m_hasSelectionBeforeLasso = false;
    updateSelectionAnimation();
    update();
}

void CanvasWidget::applySelectionMask(
    QImage mask,
    const SelectionState &previousSelection)
{
    const SelectionState nextSelection =
        selectionStateForMask(std::move(mask));
    pushSelectionChange(
        previousSelection,
        nextSelection,
        tr("Select area"));
}

CanvasWidget::SelectionState CanvasWidget::selectionStateForMask(
    QImage mask) const
{
    const Document &document = m_controller->document();
    const Layer *layer = document.layer(document.activeLayerId);
    if (mask.isNull() || !layer) {
        return {};
    }

    SelectionState state;
    state.mask = std::move(mask);
    state.layer = document.activeLayerId;
    QPainterPath selectionArea = outlinePath(state.mask);
    selectionArea.setFillRule(Qt::OddEvenFill);
    for (const Stroke &stroke : layer->strokes) {
        bool inside = std::any_of(
            stroke.points.cbegin(),
            stroke.points.cend(),
            [&state](const StrokePoint &point) {
                const QPoint pixel(
                    static_cast<int>(point.position.x()),
                    static_cast<int>(point.position.y()));
                return state.mask.rect().contains(pixel)
                    && state.mask.constScanLine(pixel.y())[pixel.x()] >= 128;
            });
        if (!inside && stroke.mode != StrokeMode::Fill) {
            const QPainterPath path = RenderEngine::strokePath(
                stroke,
                m_currentFrame,
                document.animationFrames,
                document.wobbleAmount);
            if (path.elementCount() == 1) {
                const QPainterPath::Element point = path.elementAt(0);
                QPainterPath dot;
                const qreal radius = std::max(
                    0.5,
                    stroke.width * 0.5 + document.wobbleAmount);
                dot.addEllipse(QPointF(point.x, point.y), radius, radius);
                inside = selectionArea.intersects(dot);
            } else if (!path.isEmpty()) {
                qreal visualWidth = stroke.width;
                if (stroke.brush.engine == BrushEngine::Spray) {
                    visualWidth *= std::max(
                        1.0,
                        stroke.brush.scatter
                            + stroke.brush.particleSize);
                }
                QPainterPathStroker stroker;
                stroker.setCapStyle(Qt::RoundCap);
                stroker.setJoinStyle(Qt::RoundJoin);
                stroker.setWidth(std::max(
                    1.0,
                    visualWidth + document.wobbleAmount * 2.0));
                inside = selectionArea.intersects(
                    stroker.createStroke(path));
            }
        }
        if (inside) {
            state.strokes.insert(stroke.id);
        }
    }
    return state;
}

CanvasWidget::SelectionState CanvasWidget::currentSelectionState() const
{
    return {m_selectedStrokes, m_selectionLayer, m_selectionMask};
}

CanvasWidget::SelectionSnapshot CanvasWidget::selectionSnapshot(
    const SelectionState &state) const
{
    SelectionSnapshot snapshot;
    snapshot.strokes = state.strokes;
    snapshot.layer = state.layer;
    if (!state.mask.isNull()) {
        snapshot.maskSize = state.mask.size();
        const QByteArray bytes(
            reinterpret_cast<const char *>(state.mask.constBits()),
            state.mask.sizeInBytes());
        snapshot.compressedMask = qCompress(bytes, 6);
    }
    return snapshot;
}

CanvasWidget::SelectionState CanvasWidget::selectionStateFromSnapshot(
    const SelectionSnapshot &snapshot) const
{
    SelectionState state;
    state.strokes = snapshot.strokes;
    state.layer = snapshot.layer;
    if (!snapshot.maskSize.isValid()) {
        return state;
    }
    QImage mask(snapshot.maskSize, QImage::Format_Grayscale8);
    const QByteArray bytes = qUncompress(snapshot.compressedMask);
    if (mask.isNull()
        || bytes.size() != mask.sizeInBytes()) {
        return {};
    }
    std::memcpy(
        mask.bits(),
        bytes.constData(),
        static_cast<std::size_t>(bytes.size()));
    state.mask = std::move(mask);
    return state;
}

void CanvasWidget::restoreSelectionState(const SelectionState &state)
{
    const Document &document = m_controller->document();
    const Layer *layer = document.layer(state.layer);
    if (state.mask.isNull()
        || !layer
        || state.mask.size() != document.size) {
        clearSelection();
        return;
    }

    m_selectedStrokes.clear();
    for (const Stroke &stroke : layer->strokes) {
        if (state.strokes.contains(stroke.id)) {
            m_selectedStrokes.insert(stroke.id);
        }
    }
    m_selectionLayer = state.layer;
    m_selectionMask = state.mask;
    m_movingSelection = false;
    m_moveDelta = QPointF();
    rebuildSelectionOutline();
    updateSelectionAnimation();
    emit interactionMessage(
        m_selectedStrokes.isEmpty()
            ? tr("No strokes in the selected area.")
            : tr("%n stroke(s) selected. Drag to move, press Delete to "
                 "remove.",
                 nullptr,
                 static_cast<int>(m_selectedStrokes.size())));
    update();
}

void CanvasWidget::pushSelectionChange(
    const SelectionState &previousSelection,
    const SelectionState &nextSelection,
    const QString &text)
{
    const bool bothEmpty =
        previousSelection.mask.isNull() && nextSelection.mask.isNull();
    if (bothEmpty) {
        restoreSelectionState(nextSelection);
        return;
    }

    const SelectionSnapshot previousSnapshot =
        selectionSnapshot(previousSelection);
    const SelectionSnapshot nextSnapshot =
        selectionSnapshot(nextSelection);
    QPointer<CanvasWidget> canvas(this);
    m_controller->pushTransientCommand(
        text,
        [canvas, nextSnapshot]() {
            if (canvas) {
                canvas->restoreSelectionState(
                    canvas->selectionStateFromSnapshot(nextSnapshot));
            }
        },
        [canvas, previousSnapshot]() {
            if (canvas) {
                canvas->restoreSelectionState(
                    canvas->selectionStateFromSnapshot(previousSnapshot));
            }
        });
}

void CanvasWidget::computeWandSelection(const QPointF &documentPosition)
{
    const SelectionState previousSelection = currentSelectionState();
    clearSelection();
    const QSize size = m_controller->document().size;
    const QRectF bounds(QPointF(0.0, 0.0), QSizeF(size));
    if (!bounds.contains(documentPosition)) {
        pushSelectionChange(previousSelection, {}, tr("Deselect"));
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
        pushSelectionChange(previousSelection, {}, tr("Deselect"));
        emit interactionMessage(
            tr("Click an empty area surrounded by lines to select it."));
        return;
    }
    applySelectionMask(mask, previousSelection);
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
    fillStroke.brush = m_brushSettings;
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
    update();
}

void CanvasWidget::clearSelection()
{
    if (m_selectedStrokes.isEmpty()
        && m_selectionMask.isNull()
        && !m_movingSelection) {
        updateSelectionAnimation();
        return;
    }
    m_selectedStrokes.clear();
    m_selectionMask = QImage();
    m_selectionOutline = QPainterPath();
    m_selectionLayer = {};
    m_movingSelection = false;
    m_moveDelta = QPointF();
    updateSelectionAnimation();
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

void CanvasWidget::translateSelectionOverlay(
    const QUuid &layerId,
    const QVector<QUuid> &strokeIds,
    const QPointF &delta)
{
    if (m_selectionMask.isNull() || m_selectionLayer != layerId) {
        return;
    }

    const QSet<QUuid> moved(strokeIds.cbegin(), strokeIds.cend());
    const bool affectsSelection = std::any_of(
        m_selectedStrokes.cbegin(),
        m_selectedStrokes.cend(),
        [&moved](const QUuid &id) {
            return moved.contains(id);
        });
    const QPoint shift(qRound(delta.x()), qRound(delta.y()));
    if (!affectsSelection || shift.isNull()) {
        return;
    }

    QImage movedMask(m_selectionMask.size(), m_selectionMask.format());
    movedMask.fill(0);
    QPainter painter(&movedMask);
    painter.drawImage(shift, m_selectionMask);
    painter.end();
    m_selectionMask = std::move(movedMask);
    rebuildSelectionOutline();
    update();
}

void CanvasWidget::rebuildSelectionOutline()
{
    m_selectionOutline = outlinePath(m_selectionMask);
}

void CanvasWidget::updateSelectionAnimation()
{
    const bool active = m_lassoActive || !m_selectionMask.isNull();
    if (active && !m_selectionAnimationTimer.isActive()) {
        m_selectionAnimationTimer.start();
    } else if (!active && m_selectionAnimationTimer.isActive()) {
        m_selectionAnimationTimer.stop();
        m_selectionDashOffset = 0.0;
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
    painter.save();
    painter.setTransform(transform);

    if (m_lassoActive && m_lassoPoints.size() >= 2) {
        QPainterPath lassoPath;
        lassoPath.moveTo(m_lassoPoints.first());
        for (int index = 1; index < m_lassoPoints.size(); ++index) {
            lassoPath.lineTo(m_lassoPoints[index]);
        }
        drawSelectionPath(painter, lassoPath, m_selectionDashOffset);
    }

    if (!m_selectionOutline.isEmpty()) {
        painter.translate(m_moveDelta);
        drawSelectionPath(
            painter,
            m_selectionOutline,
            m_selectionDashOffset);
    }
    painter.restore();
}

}
