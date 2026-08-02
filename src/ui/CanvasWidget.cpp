#include "ui/CanvasWidget.hpp"

#include "brush/BrushPreset.hpp"
#include "brush/EraserPreset.hpp"
#include "document/DocumentLimits.hpp"
#include "document/SelectionOperation.hpp"
#include "document/SelectionVisibility.hpp"
#include "document/StrokeMask.hpp"
#include "render/PreviewRenderPolicy.hpp"
#include "render/RenderEngine.hpp"
#include "ui/SelectionActionBar.hpp"
#include "ui/Theme.hpp"

#include <QEnterEvent>
#include <QHash>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPointer>
#include <QRandomGenerator>
#include <QResizeEvent>
#include <QTabletEvent>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace wobble
{

namespace
{

constexpr qreal canvasMargin = 32.0;
constexpr qreal minimumZoom = 0.01;
constexpr qreal maximumZoom = 16.0;
constexpr qreal keyboardZoomStep = 1.25;
constexpr qreal dragZoomDoublingDistance = 120.0;
constexpr int checkerSize = 12;

const QBrush &checkerBrush()
{
    static const QBrush brush = []()
    {
        QImage tile(checkerSize * 2, checkerSize * 2, QImage::Format_RGB32);
        tile.fill(QColor(238, 238, 238));
        QPainter painter(&tile);
        painter.fillRect(
            checkerSize, 0, checkerSize, checkerSize, QColor(210, 210, 210));
        painter.fillRect(
            0, checkerSize, checkerSize, checkerSize, QColor(210, 210, 210));
        return QBrush(tile);
    }();
    return brush;
}

bool fuzzyIdentity(const QTransform &transform)
{
    return qFuzzyCompare(transform.m11(), 1.0) && qFuzzyIsNull(transform.m12())
           && qFuzzyIsNull(transform.m13()) && qFuzzyIsNull(transform.m21())
           && qFuzzyCompare(transform.m22(), 1.0)
           && qFuzzyIsNull(transform.m23()) && qFuzzyIsNull(transform.m31())
           && qFuzzyIsNull(transform.m32())
           && qFuzzyCompare(transform.m33(), 1.0);
}

qreal pointDistance(const QPointF &a, const QPointF &b)
{
    return std::hypot(a.x() - b.x(), a.y() - b.y());
}

bool documentHasStrokes(const Document &document)
{
    for (const Layer &layer : document.layers)
    {
        if (!layer.strokes.isEmpty())
        {
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
        static_cast<quint32>(point >> 32U), static_cast<quint32>(point));
}

QPainterPath outlinePath(const QImage &mask)
{
    QHash<quint64, QVector<quint64>> edges;
    const auto inside = [&mask](int x, int y)
    {
        return x >= 0 && y >= 0 && x < mask.width() && y < mask.height()
               && mask.constScanLine(y)[x] >= 128;
    };
    const auto addEdge = [&edges](int x1, int y1, int x2, int y2)
    {
        edges[encodedPoint(x1, y1)].append(encodedPoint(x2, y2));
    };

    for (int y = 0; y < mask.height(); ++y)
    {
        const uchar *line = mask.constScanLine(y);
        for (int x = 0; x < mask.width(); ++x)
        {
            if (line[x] < 128)
            {
                continue;
            }
            if (!inside(x, y - 1))
            {
                addEdge(x, y, x + 1, y);
            }
            if (!inside(x + 1, y))
            {
                addEdge(x + 1, y, x + 1, y + 1);
            }
            if (!inside(x, y + 1))
            {
                addEdge(x + 1, y + 1, x, y + 1);
            }
            if (!inside(x - 1, y))
            {
                addEdge(x, y + 1, x, y);
            }
        }
    }

    QPainterPath path;
    while (!edges.isEmpty())
    {
        auto first = edges.begin();
        const quint64 start = first.key();
        quint64 current = start;
        path.moveTo(decodedPoint(start));

        do
        {
            auto edge = edges.find(current);
            if (edge == edges.end())
            {
                break;
            }
            const quint64 next = edge.value().takeLast();
            if (edge.value().isEmpty())
            {
                edges.erase(edge);
            }
            path.lineTo(decodedPoint(next));
            current = next;
        } while (current != start);

        if (current == start)
        {
            path.closeSubpath();
        }
    }
    return path;
}

void drawSelectionPath(
    QPainter &painter, const QPainterPath &path, qreal dashOffset)
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

CanvasWidget::CanvasWidget(DocumentController *controller, QWidget *parent)
    : QWidget(parent)
    , m_controller(controller)
{
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_AcceptTouchEvents, false);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setTabletTracking(true);
    setCursor(Qt::BlankCursor);
    m_frameCache.setMaxCost(PreviewRenderPolicy::maximumCacheKiB);
    const BrushPreset &defaultPreset = BrushPresetCatalog::defaultPreset();
    m_brushPresetId = defaultPreset.id;
    m_brushSettings = defaultPreset.settings;
    m_brushWidth = defaultPreset.defaultSize;
    m_brushPresetWidths.insert(m_brushPresetId, m_brushWidth);
    const EraserPreset &defaultEraser = EraserPresetCatalog::defaultPreset();
    m_eraserPresetId = defaultEraser.id;
    m_eraserSettings = defaultEraser.settings;
    m_eraserWidth = defaultEraser.defaultSize;
    m_eraserPresetWidths.insert(m_eraserPresetId, m_eraserWidth);

    connect(m_controller,
        &DocumentController::documentChanged,
        this,
        [this]()
        {
            cancelSelectionTransformForBoundary(
                tr("The pending selection transform was canceled because "
                   "the document changed."));
            invalidateFrames();
            pruneSelection();
        });
    connect(m_controller,
        &DocumentController::documentReplaced,
        this,
        &CanvasWidget::clearSelection);
    connect(m_controller,
        &DocumentController::selectionHistoryStateRequested,
        this,
        [this](const QUuid &layerId, const QImage &mask)
        {
            restoreSelectionState({{}, layerId, mask});
        });
    connect(m_controller,
        &DocumentController::strokesTransformed,
        this,
        &CanvasWidget::transformSelectionOverlay);
    connect(m_controller,
        &DocumentController::strokesDuplicated,
        this,
        &CanvasWidget::handleStrokesDuplicated);
    connect(m_controller,
        &DocumentController::selectionOverlayTransition,
        this,
        &CanvasWidget::handleSelectionOverlayTransition);
    connect(m_controller,
        &DocumentController::canvasResized,
        this,
        &CanvasWidget::handleCanvasResized);
    connect(m_controller,
        &DocumentController::activeLayerChanged,
        this,
        [this](const QUuid &layerId)
        {
            if (!m_selectionLayer.isNull() && m_selectionLayer != layerId)
            {
                clearSelection();
            }
        });
    connect(&m_animationTimer,
        &QTimer::timeout,
        this,
        [this]()
        {
            advanceFrame();
        });
    m_selectionAnimationTimer.setInterval(120);
    connect(&m_selectionAnimationTimer,
        &QTimer::timeout,
        this,
        [this]()
        {
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

qreal CanvasWidget::brushPresetWidth(const QString &presetId) const
{
    const BrushPreset *preset = BrushPresetCatalog::find(presetId);
    if (!preset)
    {
        return 0.0;
    }
    return std::clamp(
        m_brushPresetWidths.value(preset->id, preset->defaultSize),
        DocumentLimits::minimumStrokeWidth,
        DocumentLimits::maximumStrokeWidth);
}

qreal CanvasWidget::eraserWidth() const
{
    return m_eraserWidth;
}

qreal CanvasWidget::eraserPresetWidth(const QString &presetId) const
{
    const EraserPreset *preset = EraserPresetCatalog::find(presetId);
    if (!preset)
    {
        return 0.0;
    }
    return std::clamp(
        m_eraserPresetWidths.value(preset->id, preset->defaultSize),
        DocumentLimits::minimumStrokeWidth,
        DocumentLimits::maximumStrokeWidth);
}

qreal CanvasWidget::brushRoughness() const
{
    return m_brushRoughness;
}

qreal CanvasWidget::brushStabilization() const
{
    return brushPresetStabilization(m_brushPresetId);
}

qreal CanvasWidget::brushPresetStabilization(const QString &presetId) const
{
    const BrushPreset *preset = BrushPresetCatalog::find(presetId);
    if (!preset)
    {
        return 0.0;
    }
    return std::clamp(
        m_brushPresetStabilizations.value(preset->id, 0.0), 0.0, 1.0);
}

qreal CanvasWidget::eraserStabilization() const
{
    return eraserPresetStabilization(m_eraserPresetId);
}

qreal CanvasWidget::eraserPresetStabilization(const QString &presetId) const
{
    const EraserPreset *preset = EraserPresetCatalog::find(presetId);
    if (!preset)
    {
        return 0.0;
    }
    return std::clamp(
        m_eraserPresetStabilizations.value(preset->id, 0.0), 0.0, 1.0);
}

bool CanvasWidget::brushAntialiasing() const
{
    return m_brushAntialiasing;
}

bool CanvasWidget::isWobbleAnimationEnabled() const
{
    return m_wobbleAnimationEnabled;
}

Document CanvasWidget::displayDocument() const
{
    Document document = m_controller->document();
    if (!m_wobbleAnimationEnabled)
    {
        document.wobbleAmount = 0.0;
    }
    return document;
}

QString CanvasWidget::brushPresetId() const
{
    return m_brushPresetId;
}

QString CanvasWidget::eraserPresetId() const
{
    return m_eraserPresetId;
}

CanvasWidget::WandReference CanvasWidget::wandReference() const
{
    return m_wandReference;
}

CanvasWidget::SelectionShape CanvasWidget::selectionShape() const
{
    return m_selectionShape;
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
    return m_zoom;
}

bool CanvasWidget::isCanvasMirrored() const
{
    return m_canvasMirrored;
}

bool CanvasWidget::hasSelection() const
{
    return !m_selectionMask.isNull();
}

bool CanvasWidget::hasTransformableSelection() const
{
    return !m_selectedStrokes.isEmpty();
}

bool CanvasWidget::hasEditableStrokeSelection() const
{
    return !selectedStrokeIds().isEmpty();
}

QUuid CanvasWidget::selectionLayerId() const
{
    return m_selectionLayer;
}

QVector<QUuid> CanvasWidget::selectedStrokeIds() const
{
    const Document &document = m_controller->document();
    const Layer *layer = document.layer(m_selectionLayer);
    if (!layer || m_selectionMask.isNull())
    {
        return {};
    }
    const qint64 maskKey = m_selectionMask.cacheKey();
    if (m_editableStrokeMaskKey != maskKey
        || m_editableStrokeLayer != m_selectionLayer
        || m_editableStrokeFrame != m_currentFrame)
    {
        m_editableStrokeIds = SelectionVisibility::editableStrokeIds(
            document, *layer, m_selectionMask, m_currentFrame);
        m_editableStrokeMaskKey = maskKey;
        m_editableStrokeLayer = m_selectionLayer;
        m_editableStrokeFrame = m_currentFrame;
    }
    return m_editableStrokeIds;
}

bool CanvasWidget::selectionMoveMode() const
{
    return m_selectionMoveMode;
}

bool CanvasWidget::hasSelectionTransformSession() const
{
    return m_selectionTransformSession.active;
}

bool CanvasWidget::hasPendingSelectionTransform() const
{
    return m_selectionTransformSession.active
           && !fuzzyIdentity(m_selectionTransformSession.transform);
}

QTransform CanvasWidget::pendingSelectionTransform() const
{
    return m_selectionTransformSession.active
               ? m_selectionTransformSession.transform
               : QTransform();
}

Document CanvasWidget::documentWithPendingSelectionTransform() const
{
    Document document = m_controller->document();
    if (!hasPendingSelectionTransform())
    {
        return document;
    }
    Layer *layer = document.layer(m_selectionTransformSession.layer);
    if (!layer || layer->kind != LayerKind::Paint)
    {
        return document;
    }
    Stroke stroke;
    stroke.mode = StrokeMode::PixelSelection;
    stroke.pixelSelectionOp = m_selectionTransformSession.previewOperation;
    layer->strokes.append(std::move(stroke));
    return document;
}

bool CanvasWidget::scaleSelection(qreal factor)
{
    if (!std::isfinite(factor) || factor <= 0.0 || qFuzzyCompare(factor, 1.0)
        || !hasTransformableSelection())
    {
        return false;
    }
    setSelectionMoveMode(false);
    const bool alreadyActive = hasSelectionTransformSession();
    if (!beginSelectionTransformSession())
    {
        return false;
    }
    const QPointF center = displayedSelectionBounds().center();
    QTransform delta;
    delta.translate(center.x(), center.y());
    delta.scale(factor, factor);
    delta.translate(-center.x(), -center.y());
    if (!setPendingSelectionTransform(
            delta * m_selectionTransformSession.transform))
    {
        if (!alreadyActive)
        {
            resetSelectionTransformSession();
        }
        emit interactionMessage(tr("The selection could not be scaled."));
        return false;
    }
    return true;
}

bool CanvasWidget::rotateSelection(qreal degrees)
{
    if (!std::isfinite(degrees) || qFuzzyIsNull(degrees)
        || !hasTransformableSelection())
    {
        return false;
    }
    setSelectionMoveMode(false);
    const bool alreadyActive = hasSelectionTransformSession();
    if (!beginSelectionTransformSession())
    {
        return false;
    }
    const QPointF center = displayedSelectionBounds().center();
    QTransform delta;
    delta.translate(center.x(), center.y());
    delta.rotate(degrees);
    delta.translate(-center.x(), -center.y());
    if (!setPendingSelectionTransform(
            delta * m_selectionTransformSession.transform))
    {
        if (!alreadyActive)
        {
            resetSelectionTransformSession();
        }
        emit interactionMessage(tr("The selection could not be rotated."));
        return false;
    }
    return true;
}

bool CanvasWidget::flipSelectionHorizontally()
{
    return flipSelection(true);
}

bool CanvasWidget::flipSelectionVertically()
{
    return flipSelection(false);
}

bool CanvasWidget::applySelectionTransform()
{
    if (m_movingSelection)
    {
        commitSelectionMove();
    }
    if (!hasPendingSelectionTransform())
    {
        return false;
    }
    setSelectionMoveMode(false);

    const FloatingTransformSession session = m_selectionTransformSession;
    resetSelectionTransformSession();
    const bool applied = m_controller->transformSelection(session.layer,
        session.strokeIds,
        session.transform,
        session.sourceMask);
    if (!applied)
    {
        m_selectionTransformSession = session;
        emit selectionTransformSessionChanged(true, true);
        updateSelectionActionBar();
        update();
        emit interactionMessage(
            tr("The selection transform could not be applied."));
        return false;
    }
    emit interactionMessage(tr("Selection transform applied."));
    return true;
}

void CanvasWidget::cancelSelectionTransform()
{
    if (!hasSelectionTransformSession())
    {
        return;
    }
    cancelSelectionMove();
    resetSelectionTransformSession();
    setSelectionMoveMode(false);
    emit interactionMessage(tr("Selection transform canceled."));
}

bool CanvasWidget::duplicateSelection()
{
    if (m_selectedStrokes.isEmpty())
    {
        return false;
    }
    cancelSelectionTransformForBoundary(
        tr("The pending transform was canceled before duplicating."));
    setSelectionMoveMode(false);
    const QPointF delta = clampedSelectionDelta(QPointF(12.0, 12.0));
    const bool duplicated = m_controller->duplicateStrokes(m_selectionLayer,
        QVector<QUuid>(m_selectedStrokes.cbegin(), m_selectedStrokes.cend()),
        delta,
        m_selectionMask);
    if (!duplicated)
    {
        emit interactionMessage(tr("The selection could not be duplicated."));
    }
    return duplicated;
}

bool CanvasWidget::deleteSelection()
{
    if (m_selectedStrokes.isEmpty() || m_selectionMask.isNull())
    {
        return false;
    }
    cancelSelectionTransformForBoundary(
        tr("The pending transform was canceled before deleting."));
    setSelectionMoveMode(false);
    const bool removed = m_controller->removeSelectedContent(m_selectionLayer,
        QVector<QUuid>(m_selectedStrokes.cbegin(), m_selectedStrokes.cend()),
        m_selectionMask);
    if (removed)
    {
        clearSelection();
    }
    else
    {
        emit interactionMessage(
            tr("The selected content could not be deleted."));
    }
    return removed;
}

void CanvasWidget::deselectSelection()
{
    if (m_selectionMask.isNull())
    {
        setSelectionMoveMode(false);
        return;
    }
    cancelSelectionTransformForBoundary(
        tr("The pending transform was canceled before deselecting."));
    setSelectionMoveMode(false);
    pushSelectionChange(currentSelectionState(), {}, tr("Deselect"));
}

void CanvasWidget::setSelectionActionBar(SelectionActionBar *actionBar)
{
    if (m_selectionActionBar == actionBar)
    {
        updateSelectionActionBar();
        return;
    }
    if (m_selectionActionBar)
    {
        m_selectionActionBar->hide();
    }
    m_selectionActionBar = actionBar;
    if (m_selectionActionBar)
    {
        m_selectionActionBar->setParent(this);
        m_selectionActionBar->hide();
    }
    updateSelectionActionBar();
}

void CanvasWidget::releaseTransientRenderCaches()
{
    invalidateFrames();
}

void CanvasWidget::setTool(Tool tool)
{
    if (m_tool == tool)
    {
        return;
    }
    cancelSelectionTransformForBoundary(tr(
        "The pending selection transform was canceled when changing tools."));
    cancelStroke();
    endColorPick();
    setSelectionMoveMode(false);
    if (m_areaSelectionActive)
    {
        const SelectionState previousSelection =
            m_hasSelectionBeforeArea ? m_selectionBeforeArea : SelectionState();
        cancelAreaSelection();
        restoreSelectionState(previousSelection);
    }
    m_tool = tool;
    emit toolChanged(tool);
    updateCursor();
    update();
}

void CanvasWidget::setSelectionMoveMode(bool enabled)
{
    const bool next = enabled && hasTransformableSelection();
    if (m_selectionMoveMode == next)
    {
        updateSelectionActionBar();
        return;
    }
    if (!next)
    {
        cancelSelectionMove();
    }
    m_selectionMoveMode = next;
    emit selectionMoveModeChanged(next);
    updateCursor();
    updateSelectionActionBar();
    update();
}

void CanvasWidget::handleEscape()
{
    if (m_movingSelection)
    {
        cancelSelectionMove();
    }
    else if (hasSelectionTransformSession())
    {
        cancelSelectionTransform();
    }
    else if (m_areaSelectionActive)
    {
        const SelectionState previousSelection =
            m_hasSelectionBeforeArea ? m_selectionBeforeArea : SelectionState();
        cancelAreaSelection();
        restoreSelectionState(previousSelection);
    }
    else if (m_selectionMoveMode)
    {
        setSelectionMoveMode(false);
    }
    else if (m_drawing || m_panning || m_zoomDragging || m_pickingColor)
    {
        cancelStroke();
        endPan();
        endZoomDrag();
        endColorPick();
        m_tabletSequence = false;
    }
    else
    {
        deselectSelection();
    }
}

void CanvasWidget::setBrushColor(const QColor &color)
{
    if (!color.isValid() || m_brushColor == color)
    {
        return;
    }
    m_brushColor = color;
    emit brushColorChanged(color);
    update();
}

void CanvasWidget::setBrushWidth(qreal width)
{
    if (!std::isfinite(width))
    {
        return;
    }
    const qreal normalized = std::clamp(width,
        DocumentLimits::minimumStrokeWidth,
        DocumentLimits::maximumStrokeWidth);
    if (qFuzzyCompare(m_brushWidth, normalized))
    {
        return;
    }
    m_brushWidth = normalized;
    if (!m_brushPresetId.isEmpty())
    {
        m_brushPresetWidths.insert(m_brushPresetId, normalized);
    }
    emit brushWidthChanged(normalized);
    update();
}

void CanvasWidget::setBrushPresetWidth(const QString &presetId, qreal width)
{
    const BrushPreset *preset = BrushPresetCatalog::find(presetId);
    if (!preset || !std::isfinite(width))
    {
        return;
    }
    const qreal normalized = std::clamp(width,
        DocumentLimits::minimumStrokeWidth,
        DocumentLimits::maximumStrokeWidth);
    if (m_brushPresetId == preset->id)
    {
        setBrushWidth(normalized);
        return;
    }
    m_brushPresetWidths.insert(preset->id, normalized);
}

void CanvasWidget::setEraserWidth(qreal width)
{
    if (!std::isfinite(width))
    {
        return;
    }
    const qreal normalized = std::clamp(width,
        DocumentLimits::minimumStrokeWidth,
        DocumentLimits::maximumStrokeWidth);
    if (qFuzzyCompare(m_eraserWidth, normalized))
    {
        return;
    }
    m_eraserWidth = normalized;
    if (!m_eraserPresetId.isEmpty())
    {
        m_eraserPresetWidths.insert(m_eraserPresetId, normalized);
    }
    emit eraserWidthChanged(normalized);
    update();
}

void CanvasWidget::setEraserPresetWidth(const QString &presetId, qreal width)
{
    const EraserPreset *preset = EraserPresetCatalog::find(presetId);
    if (!preset || !std::isfinite(width))
    {
        return;
    }
    const qreal normalized = std::clamp(width,
        DocumentLimits::minimumStrokeWidth,
        DocumentLimits::maximumStrokeWidth);
    if (m_eraserPresetId == preset->id)
    {
        setEraserWidth(normalized);
        return;
    }
    m_eraserPresetWidths.insert(preset->id, normalized);
}

void CanvasWidget::setBrushRoughness(qreal roughness)
{
    if (!std::isfinite(roughness))
    {
        return;
    }
    const qreal normalized = std::clamp(roughness,
        DocumentLimits::minimumBrushWobbleScale,
        DocumentLimits::maximumBrushWobbleScale);
    if (qFuzzyCompare(m_brushRoughness, normalized))
    {
        return;
    }
    m_brushRoughness = normalized;
    emit brushRoughnessChanged(normalized);
    update();
}

void CanvasWidget::setBrushStabilization(qreal strength)
{
    setBrushPresetStabilization(m_brushPresetId, strength);
}

void CanvasWidget::setBrushPresetStabilization(
    const QString &presetId, qreal strength)
{
    const BrushPreset *preset = BrushPresetCatalog::find(presetId);
    if (!preset || !std::isfinite(strength))
    {
        return;
    }
    const qreal normalized = std::clamp(strength, 0.0, 1.0);
    if (qFuzzyCompare(
            1.0 + brushPresetStabilization(preset->id), 1.0 + normalized))
    {
        return;
    }
    m_brushPresetStabilizations.insert(preset->id, normalized);
    if (m_brushPresetId == preset->id)
    {
        emit brushStabilizationChanged(normalized);
    }
}

void CanvasWidget::setEraserStabilization(qreal strength)
{
    setEraserPresetStabilization(m_eraserPresetId, strength);
}

void CanvasWidget::setEraserPresetStabilization(
    const QString &presetId, qreal strength)
{
    const EraserPreset *preset = EraserPresetCatalog::find(presetId);
    if (!preset || !std::isfinite(strength))
    {
        return;
    }
    const qreal normalized = std::clamp(strength, 0.0, 1.0);
    if (qFuzzyCompare(
            1.0 + eraserPresetStabilization(preset->id), 1.0 + normalized))
    {
        return;
    }
    m_eraserPresetStabilizations.insert(preset->id, normalized);
    if (m_eraserPresetId == preset->id)
    {
        emit eraserStabilizationChanged(normalized);
    }
}

void CanvasWidget::setBrushAntialiasing(bool antialiasing)
{
    if (m_brushAntialiasing == antialiasing)
    {
        return;
    }
    m_brushAntialiasing = antialiasing;
    emit brushAntialiasingChanged(antialiasing);
    update();
}

void CanvasWidget::setWobbleAnimationEnabled(bool enabled)
{
    if (m_wobbleAnimationEnabled == enabled)
    {
        return;
    }
    m_wobbleAnimationEnabled = enabled;
    if (!enabled)
    {
        setAnimating(false);
    }
    invalidateFrames();
}

void CanvasWidget::setBrushPreset(const QString &presetId)
{
    const BrushPreset *preset = BrushPresetCatalog::find(presetId);
    if (!preset || m_brushPresetId == preset->id)
    {
        return;
    }
    cancelStroke();
    m_brushPresetId = preset->id;
    m_brushSettings = preset->settings;
    const qreal nextWidth =
        std::clamp(m_brushPresetWidths.value(preset->id, preset->defaultSize),
            DocumentLimits::minimumStrokeWidth,
            DocumentLimits::maximumStrokeWidth);
    m_brushPresetWidths.insert(preset->id, nextWidth);
    if (!qFuzzyCompare(m_brushWidth, nextWidth))
    {
        m_brushWidth = nextWidth;
        emit brushWidthChanged(nextWidth);
    }
    emit brushPresetChanged(preset->id);
    emit brushStabilizationChanged(brushStabilization());
    update();
}

void CanvasWidget::setEraserPreset(const QString &presetId)
{
    const EraserPreset *preset = EraserPresetCatalog::find(presetId);
    if (!preset || m_eraserPresetId == preset->id)
    {
        return;
    }
    cancelStroke();
    m_eraserPresetId = preset->id;
    m_eraserSettings = preset->settings;
    const qreal nextWidth =
        std::clamp(m_eraserPresetWidths.value(preset->id, preset->defaultSize),
            DocumentLimits::minimumStrokeWidth,
            DocumentLimits::maximumStrokeWidth);
    m_eraserPresetWidths.insert(preset->id, nextWidth);
    if (!qFuzzyCompare(m_eraserWidth, nextWidth))
    {
        m_eraserWidth = nextWidth;
        emit eraserWidthChanged(nextWidth);
    }
    emit eraserPresetChanged(preset->id);
    emit eraserStabilizationChanged(eraserStabilization());
    update();
}

void CanvasWidget::setWandReference(WandReference reference)
{
    switch (reference)
    {
    case WandReference::ActiveLayer:
    case WandReference::ReferenceLayers:
    case WandReference::AllVisibleLayers:
        break;
    default:
        return;
    }
    if (m_wandReference == reference)
    {
        return;
    }
    m_wandReference = reference;
    emit wandReferenceChanged(reference);
}

void CanvasWidget::setSelectionShape(SelectionShape shape)
{
    switch (shape)
    {
    case SelectionShape::Freehand:
    case SelectionShape::Rectangle:
    case SelectionShape::Ellipse:
        break;
    default:
        return;
    }
    if (m_selectionShape == shape)
    {
        return;
    }
    if (m_areaSelectionActive)
    {
        const SelectionState previousSelection =
            m_hasSelectionBeforeArea ? m_selectionBeforeArea : SelectionState();
        cancelAreaSelection();
        restoreSelectionState(previousSelection);
    }
    m_selectionShape = shape;
    emit selectionShapeChanged(shape);
    update();
}

void CanvasWidget::setAnimating(bool animating)
{
    if (animating && !m_wobbleAnimationEnabled)
    {
        return;
    }
    if (m_animating == animating)
    {
        return;
    }
    const QSize previousPreviewSize = previewRenderSize();
    m_animating = animating;
    if (previewRenderSize() != previousPreviewSize)
    {
        invalidateFrames();
    }
    if (m_animating)
    {
        updateTimerInterval();
        m_animationTimer.start();
    }
    else
    {
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

void CanvasWidget::setGroupSelectionActive(bool active)
{
    if (m_groupSelectionActive == active)
    {
        return;
    }
    m_groupSelectionActive = active;
    updateCursor();
    update();
}

void CanvasWidget::fitToWindow()
{
    m_zoom = fitZoom();
    m_pan = QPointF();
    notifyZoomChanged();
    update();
}

void CanvasWidget::resetZoom()
{
    zoomToward(1.0, zoomAnchorPosition());
}

void CanvasWidget::setZoomPercent(int percent)
{
    zoomToward(std::clamp(percent / 100.0, minimumZoom, maximumZoom),
        zoomAnchorPosition());
}

void CanvasWidget::zoomIn()
{
    zoomToward(m_zoom * keyboardZoomStep, zoomAnchorPosition());
}

void CanvasWidget::zoomOut()
{
    zoomToward(m_zoom / keyboardZoomStep, zoomAnchorPosition());
}

void CanvasWidget::setCanvasMirrored(bool mirrored)
{
    if (m_canvasMirrored == mirrored)
    {
        return;
    }
    cancelStroke();
    endPan();
    endZoomDrag();
    endColorPick();
    m_canvasMirrored = mirrored;
    updatePointerPosition(m_pointerWidgetPosition);
    emit canvasMirroredChanged(mirrored);
    update();
}

void CanvasWidget::toggleCanvasMirrored()
{
    setCanvasMirrored(!m_canvasMirrored);
}

void CanvasWidget::setCurrentFrame(int frame)
{
    const int frameCount =
        std::max(1, m_controller->document().animationFrames);
    const int normalized = ((frame % frameCount) + frameCount) % frameCount;
    if (normalized == m_currentFrame)
    {
        return;
    }
    m_currentFrame = normalized;
    invalidateActiveStrokePreview();
    emit currentFrameChanged(normalized);
    update();
}

void CanvasWidget::setPanModifierActive(bool active)
{
    if (m_spacePressed == active)
    {
        return;
    }
    m_spacePressed = active;
    updateCursor();
    update();
}

void CanvasWidget::cancelActiveInteraction()
{
    const bool restoreAreaSelection =
        m_areaSelectionActive && m_hasSelectionBeforeArea;
    const SelectionState selectionBeforeArea =
        restoreAreaSelection ? m_selectionBeforeArea : SelectionState();

    cancelStroke();
    cancelSelectionMove();
    cancelAreaSelection();
    if (restoreAreaSelection)
    {
        restoreSelectionState(selectionBeforeArea);
    }
    endPan();
    endZoomDrag();
    endColorPick();
    m_tabletSequence = false;
    m_tabletPointerEraser = false;
    setPanModifierActive(false);
    updateCursor();
    update();
}

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
        painter.fillRect(checkerRect, checkerBrush());
    }

    painter.save();
    painter.setTransform(transform);
    const QSize renderSize = previewRenderSize();
    QImage displayedFrame;
    QImage regionalFrame;
    QRect regionalBounds;
    bool activeStrokePreviewResolved = false;
    if (m_drawing && !m_activeStroke.points.isEmpty())
    {
        regionalFrame = activeStrokePreview(
            document, renderSize, regionalBounds, activeStrokePreviewResolved);
        if (!regionalFrame.isNull()
            && regionalBounds == QRect(QPoint(), renderSize))
        {
            displayedFrame = regionalFrame;
            regionalFrame = {};
            regionalBounds = {};
        }
    }
    else if (hasPendingSelectionTransform())
    {
        const RenderEngine::LayerSplitFrame &split =
            previewSplit(m_selectionTransformSession.layer, renderSize);
        if (split.valid)
        {
            QImage layerImage = split.layerBase;
            if (RenderEngine::replayPixelSelectionOnLayer(
                    layerImage, m_selectionTransformSession.previewOperation))
            {
                displayedFrame =
                    RenderEngine::composeLayerSplit(split, layerImage);
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
    if (displayedFrame.isNull() && regionalFrame.isNull()
        && !activeStrokePreviewResolved
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
    if (!regionalFrame.isNull() && !regionalBounds.isEmpty())
    {
        const qreal horizontalScale =
            static_cast<qreal>(document.size.width()) / renderSize.width();
        const qreal verticalScale =
            static_cast<qreal>(document.size.height()) / renderSize.height();
        const QRectF documentPreviewRect(regionalBounds.x() * horizontalScale,
            regionalBounds.y() * verticalScale,
            regionalBounds.width() * horizontalScale,
            regionalBounds.height() * verticalScale);
        const QRectF widgetPreviewRect =
            transform.mapRect(documentPreviewRect).intersected(canvasRect);
        painter.save();
        painter.resetTransform();
        painter.fillRect(widgetPreviewRect, checkerBrush());
        painter.restore();
        painter.drawImage(documentPreviewRect, regionalFrame);
    }
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
        endStroke(event->position(), 1.0, event->timestamp());
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
            endStroke(event->position(), event->pressure(), event->timestamp());
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

QTransform CanvasWidget::documentTransform() const
{
    const QSize canvasSize = m_controller->document().size;
    if (!canvasSize.isValid())
    {
        return {};
    }
    const QPointF center(width() * 0.5, height() * 0.5);
    const QPointF canvasCenter(
        canvasSize.width() * 0.5, canvasSize.height() * 0.5);

    QTransform transform;
    transform.translate(center.x() + m_pan.x(), center.y() + m_pan.y());
    transform.scale(m_canvasMirrored ? -m_zoom : m_zoom, m_zoom);
    transform.translate(-canvasCenter.x(), -canvasCenter.y());
    return transform;
}

qreal CanvasWidget::fitZoom() const
{
    const QSize canvasSize = m_controller->document().size;
    if (!canvasSize.isValid())
    {
        return 1.0;
    }
    const qreal availableWidth = std::max(1.0, width() - canvasMargin * 2.0);
    const qreal availableHeight = std::max(1.0, height() - canvasMargin * 2.0);
    return std::clamp(std::min(availableWidth / canvasSize.width(),
                          availableHeight / canvasSize.height()),
        minimumZoom,
        maximumZoom);
}

QPointF CanvasWidget::mapToDocument(
    const QPointF &widgetPosition, bool *inside) const
{
    bool invertible = false;
    const QTransform inverse = documentTransform().inverted(&invertible);
    const QPointF position =
        invertible ? inverse.map(widgetPosition) : QPointF();
    const QRectF bounds(
        QPointF(0.0, 0.0), QSizeF(m_controller->document().size));
    if (inside)
    {
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
    if (renderSize != m_cachedRenderSize)
    {
        m_frameCache.clear();
        m_cachedRenderSize = renderSize;
    }
    if (QImage *cached = m_frameCache.object(frame))
    {
        return *cached;
    }
    QImage image;
    if (m_previewSplit.valid && m_previewSplitFrame == frame
        && m_previewSplit.below.size() == renderSize)
    {
        image = RenderEngine::composeLayerSplit(
            m_previewSplit, m_previewSplit.layerBase);
    }
    if (image.isNull() && m_previewLayerRasters.valid
        && m_previewLayerRasterFrame == frame
        && m_previewLayerRasters.outputSize == renderSize)
    {
        image = RenderEngine::composeLayerRasterFrame(
            displayDocument(), m_previewLayerRasters, {}, {});
    }
    if (image.isNull())
    {
        image =
            RenderEngine::renderScaled(displayDocument(), frame, renderSize);
    }
    if (image.isNull())
    {
        return {};
    }
    const int cost = PreviewRenderPolicy::cacheCostKiB(image.sizeInBytes());
    m_frameCache.insert(frame, new QImage(image), cost);
    return image;
}

QRect CanvasWidget::visiblePreviewRect(const QSize &renderSize) const
{
    const QSize documentSize = m_controller->document().size;
    if (documentSize.isEmpty() || renderSize.isEmpty())
    {
        return {};
    }
    bool invertible = false;
    const QTransform inverse = documentTransform().inverted(&invertible);
    if (!invertible)
    {
        return QRect(QPoint(), renderSize);
    }
    const QRectF documentBounds{QPointF(), QSizeF(documentSize)};
    const QRectF visibleDocument =
        inverse.mapRect(QRectF(rect())).intersected(documentBounds);
    const qreal horizontalScale =
        static_cast<qreal>(renderSize.width()) / documentSize.width();
    const qreal verticalScale =
        static_cast<qreal>(renderSize.height()) / documentSize.height();
    const QRectF mapped(visibleDocument.x() * horizontalScale,
        visibleDocument.y() * verticalScale,
        visibleDocument.width() * horizontalScale,
        visibleDocument.height() * verticalScale);
    return mapped.toAlignedRect()
        .adjusted(-2, -2, 2, 2)
        .intersected(QRect(QPoint(), renderSize));
}

QImage CanvasWidget::activeStrokePreview(const Document &document,
    const QSize &renderSize,
    QRect &outputBounds,
    bool &resolved)
{
    const QRect visibleRect = visiblePreviewRect(renderSize);
    if (m_activeStrokePreviewResolved
        && m_activeStrokePreviewRenderSize == renderSize
        && m_activeStrokePreviewVisibleRect == visibleRect
        && m_activeStrokePreviewFrame == m_currentFrame)
    {
        outputBounds = m_activeStrokePreviewBounds;
        resolved = true;
        return m_activeStrokePreview;
    }

    QRect bounds =
        RenderEngine::strokePreviewBounds(document, m_activeStroke, renderSize);
    bounds = bounds.intersected(visibleRect);
    if (bounds.isEmpty())
    {
        m_activeStrokePreview = {};
        m_activeStrokePreviewBounds = {};
        m_activeStrokePreviewVisibleRect = visibleRect;
        m_activeStrokePreviewRenderSize = renderSize;
        m_activeStrokePreviewFrame = m_currentFrame;
        m_activeStrokePreviewResolved = true;
        outputBounds = {};
        resolved = true;
        return {};
    }
    QImage preview;
    if (document.layer(m_activeStrokeLayer))
    {
        const RenderEngine::LayerSplitFrame &split =
            previewSplit(m_activeStrokeLayer, renderSize);
        if (split.valid)
        {
            QImage layerImage = split.layerBase.copy(bounds);
            if (RenderEngine::renderStrokesOnLayerRegion(layerImage,
                    document,
                    {m_activeStroke},
                    m_currentFrame,
                    renderSize,
                    bounds,
                    &m_activeStrokeRenderCache))
            {
                preview = RenderEngine::composeLayerSplitRegion(
                    split, layerImage, bounds);
            }
        }
        if (preview.isNull())
        {
            const RenderEngine::LayerRasterFrame &rasters =
                previewLayerRasters(renderSize);
            const auto cached =
                rasters.paintLayers.constFind(m_activeStrokeLayer);
            if (rasters.valid && cached != rasters.paintLayers.cend())
            {
                QImage layerImage = cached.value().copy(bounds);
                if (RenderEngine::renderStrokesOnLayerRegion(layerImage,
                        document,
                        {m_activeStroke},
                        m_currentFrame,
                        renderSize,
                        bounds,
                        &m_activeStrokeRenderCache))
                {
                    preview =
                        RenderEngine::composeLayerRasterFrameRegion(document,
                            rasters,
                            m_activeStrokeLayer,
                            layerImage,
                            bounds);
                }
            }
        }
    }
    if (preview.isNull())
    {
        preview = interactionPreview(document, renderSize);
        bounds = preview.isNull() ? QRect() : QRect(QPoint(), renderSize);
    }
    m_activeStrokePreview = preview;
    m_activeStrokePreviewBounds = bounds;
    m_activeStrokePreviewVisibleRect = visibleRect;
    m_activeStrokePreviewRenderSize = renderSize;
    m_activeStrokePreviewFrame = m_currentFrame;
    m_activeStrokePreviewResolved = !preview.isNull();
    outputBounds = bounds;
    resolved = m_activeStrokePreviewResolved;
    return preview;
}

void CanvasWidget::invalidateActiveStrokePreview()
{
    m_activeStrokePreview = {};
    m_activeStrokePreviewBounds = {};
    m_activeStrokePreviewVisibleRect = {};
    m_activeStrokePreviewRenderSize = {};
    m_activeStrokePreviewFrame = -1;
    m_activeStrokePreviewResolved = false;
}

QImage CanvasWidget::interactionPreview(
    Document document, const QSize &renderSize) const
{
    Layer *layer = nullptr;
    if (m_drawing && !m_activeStroke.points.isEmpty())
    {
        layer = document.layer(m_activeStrokeLayer);
        if (layer)
        {
            layer->strokes.append(m_activeStroke);
        }
    }
    else if (hasPendingSelectionTransform())
    {
        layer = document.layer(m_selectionTransformSession.layer);
        if (layer)
        {
            Stroke operation;
            operation.mode = StrokeMode::PixelSelection;
            operation.pixelSelectionOp =
                m_selectionTransformSession.previewOperation;
            layer->strokes.append(std::move(operation));
        }
    }
    if (!layer || layer->kind != LayerKind::Paint)
    {
        return {};
    }
    return RenderEngine::renderScaled(document, m_currentFrame, renderSize);
}

const RenderEngine::LayerSplitFrame &CanvasWidget::previewSplit(
    const QUuid &layerId, const QSize &renderSize)
{
    if (!m_previewSplit.valid || m_previewSplitLayer != layerId
        || m_previewSplitFrame != m_currentFrame
        || m_previewSplit.below.size() != renderSize)
    {
        m_previewSplit = RenderEngine::renderLayerSplit(
            displayDocument(), m_currentFrame, renderSize, layerId);
        m_previewSplitLayer = layerId;
        m_previewSplitFrame = m_currentFrame;
    }
    return m_previewSplit;
}

const RenderEngine::LayerRasterFrame &CanvasWidget::previewLayerRasters(
    const QSize &renderSize)
{
    if (m_previewLayerRasterFrame != m_currentFrame
        || m_previewLayerRasters.outputSize != renderSize)
    {
        m_frameCache.clear();
        m_previewLayerRasters = RenderEngine::renderLayerRasterFrame(
            displayDocument(),
            m_currentFrame,
            renderSize,
            static_cast<qint64>(PreviewRenderPolicy::maximumCacheKiB) * 1024);
        m_previewLayerRasterFrame = m_currentFrame;
    }
    return m_previewLayerRasters;
}

QSize CanvasWidget::previewRenderSize() const
{
    const Document &document = m_controller->document();
    const QSize documentSize = document.size;
    const qreal displayScale =
        std::abs(documentTransform().m11()) * devicePixelRatioF();
    int retainedSurfaces = m_animating ? document.animationFrames : 1;
    if (m_drawing
        && !RenderEngine::supportsLayerSplit(document, m_activeStrokeLayer))
    {
        int paintSurfaces = 0;
        bool hasEmptyLayer = false;
        for (const Layer &layer : document.layers)
        {
            if (layer.kind != LayerKind::Paint)
            {
                continue;
            }
            if (layer.strokes.isEmpty())
            {
                hasEmptyLayer = true;
            }
            else
            {
                ++paintSurfaces;
            }
        }
        retainedSurfaces =
            std::max(retainedSurfaces, paintSurfaces + (hasEmptyLayer ? 1 : 0));
    }
    const LayerCompositionMemoryEstimate hierarchyMemory =
        RenderEngine::estimateHierarchyMemory(document, documentSize);
    if (!hierarchyMemory.valid)
    {
        return {};
    }
    return PreviewRenderPolicy::renderSize(documentSize,
        displayScale,
        retainedSurfaces,
        hierarchyMemory.peakSurfaceCount);
}

void CanvasWidget::invalidateFrames()
{
    m_frameCache.clear();
    m_cachedRenderSize = {};
    m_colorPickFrame = {};
    m_colorPickFrameIndex = -1;
    m_previewSplit = {};
    m_previewSplitLayer = {};
    m_previewSplitFrame = -1;
    m_previewLayerRasters = {};
    m_previewLayerRasterFrame = -1;
    invalidateActiveStrokePreview();
    m_activeStrokeRenderCache.clipPaths.clear();
    m_editableStrokeIds.clear();
    m_editableStrokeMaskKey = 0;
    m_editableStrokeLayer = {};
    m_editableStrokeFrame = -1;
    const int frames = std::max(1, m_controller->document().animationFrames);
    m_currentFrame %= frames;
    updateTimerInterval();
    notifyZoomChanged();
    update();
}

void CanvasWidget::updateTimerInterval()
{
    const qreal fps = std::clamp(m_controller->document().framesPerSecond,
        DocumentLimits::minimumFramesPerSecond,
        DocumentLimits::maximumFramesPerSecond);
    m_animationTimer.setInterval(std::max(1, qRound(1000.0 / fps)));
}

void CanvasWidget::advanceFrame()
{
    if (!m_animating || (m_drawing && !m_animateWhileDrawing) || m_panning
        || m_zoomDragging || m_pickingColor || m_movingSelection
        || m_areaSelectionActive)
    {
        return;
    }
    setCurrentFrame(m_currentFrame + 1);
}

void CanvasWidget::beginStroke(const QPointF &widgetPosition,
    qreal pressure,
    bool tabletEraser,
    quint64 timestamp)
{
    bool inside = false;
    const QPointF documentPosition = mapToDocument(widgetPosition, &inside);
    const Document &document = m_controller->document();
    const Layer *layer = document.layer(document.activeLayerId);
    if (!inside)
    {
        return;
    }
    if (!layer || layer->kind != LayerKind::Paint)
    {
        emit interactionMessage(tr("Add a layer before using this tool."));
        return;
    }
    if (m_groupSelectionActive)
    {
        emit interactionMessage(
            tr("Groups can't be painted on. Select a paint layer to draw."));
        return;
    }
    cancelSelectionTransformForBoundary(
        tr("The pending selection transform was canceled before drawing."));
    if (!layer->visible)
    {
        emit interactionMessage(
            tr("The active layer is hidden. Make it visible to draw."));
        return;
    }
    if (layer->opacity <= 0.0)
    {
        emit interactionMessage(
            tr("The active layer opacity is 0%. Increase it to draw."));
        return;
    }

    m_activeStroke = Stroke();
    m_activeStroke.seed = QRandomGenerator::global()->generate64();
    const bool erasing = tabletEraser || m_tool == Tool::Eraser;
    m_activeStroke.mode = erasing ? StrokeMode::Erase : StrokeMode::Paint;
    m_activeStroke.color = m_brushColor;
    m_activeStroke.width = erasing ? m_eraserWidth : m_brushWidth;
    m_activeStroke.brush = erasing ? m_eraserSettings : m_brushSettings;
    if (!erasing)
    {
        m_activeStroke.brush.wobbleScale = m_brushRoughness;
        m_activeStroke.brush.antialiasing = m_brushAntialiasing;
    }
    if (!m_selectionMask.isNull() && m_selectionLayer == document.activeLayerId)
    {
        m_activeStroke.clipMask = m_selectionMask;
    }
    m_strokeStabilizer.setStrength(
        erasing ? eraserStabilization() : brushStabilization());
    const QPointF position = m_strokeStabilizer.begin(
        clampedDocumentPosition(documentPosition), timestamp);
    m_activeStroke.points.append({position, std::clamp(pressure, 0.05, 1.0)});
    m_activeStrokeLayer = document.activeLayerId;
    m_drawing = true;
    invalidateActiveStrokePreview();
    m_activeStrokeRenderCache.clipPaths.clear();
    update();
}

void CanvasWidget::continueStroke(
    const QPointF &widgetPosition, qreal pressure, quint64 timestamp)
{
    if (!m_drawing)
    {
        return;
    }
    if (m_activeStroke.points.size() >= DocumentLimits::maximumPointsPerStroke)
    {
        return;
    }
    const QPointF position = m_strokeStabilizer.update(
        clampedDocumentPosition(mapToDocument(widgetPosition)), timestamp);
    if (pointDistance(position, m_activeStroke.points.constLast().position)
        < 0.75)
    {
        return;
    }
    m_activeStroke.points.append({position, std::clamp(pressure, 0.05, 1.0)});
    invalidateActiveStrokePreview();
    update();
}

void CanvasWidget::endStroke(
    const QPointF &widgetPosition, qreal pressure, quint64 timestamp)
{
    if (!m_drawing)
    {
        return;
    }
    continueStroke(widgetPosition, pressure, timestamp);
    const QPointF finalPosition = m_strokeStabilizer.finish(
        clampedDocumentPosition(mapToDocument(widgetPosition)), timestamp);
    const StrokePoint finalPoint{
        finalPosition, std::clamp(pressure, 0.05, 1.0)};
    if (m_activeStroke.points.size() >= DocumentLimits::maximumPointsPerStroke
        || pointDistance(
               finalPosition, m_activeStroke.points.constLast().position)
               < 0.75)
    {
        m_activeStroke.points.last() = finalPoint;
    }
    else
    {
        m_activeStroke.points.append(finalPoint);
    }
    Stroke completed = m_activeStroke;
    const QUuid layerId = m_activeStrokeLayer;
    m_drawing = false;
    m_activeStroke = Stroke();
    m_activeStrokeLayer = {};
    invalidateActiveStrokePreview();
    m_activeStrokeRenderCache.clipPaths.clear();
    m_strokeStabilizer.reset();
    commitStroke(layerId, std::move(completed));
    update();
}

void CanvasWidget::commitStroke(const QUuid &layerId, Stroke stroke)
{
    const DocumentController::AddStrokeResult result =
        m_controller->addStroke(layerId, std::move(stroke));
    switch (result)
    {
    case DocumentController::AddStrokeResult::Added:
        return;
    case DocumentController::AddStrokeResult::AddedWithResampledPoints:
        emit interactionMessage(tr("The stroke was simplified because the "
                                   "project point limit was reached."));
        return;
    case DocumentController::AddStrokeResult::RejectedInvalidLayer:
        emit interactionMessage(tr("The stroke could not be added because its "
                                   "layer is no longer available."));
        return;
    case DocumentController::AddStrokeResult::RejectedStrokeLimit:
        emit interactionMessage(tr("The stroke could not be added because the "
                                   "project stroke limit was reached."));
        return;
    case DocumentController::AddStrokeResult::RejectedPointLimit:
        emit interactionMessage(tr("The stroke could not be added because the "
                                   "project point limit was reached."));
        return;
    case DocumentController::AddStrokeResult::RejectedInvalidStroke:
    case DocumentController::AddStrokeResult::RejectedMaskLimit:
    case DocumentController::AddStrokeResult::RejectedCommit:
        emit interactionMessage(tr("The stroke could not be added."));
        return;
    }
}

void CanvasWidget::cancelStroke()
{
    if (!m_drawing)
    {
        return;
    }
    m_drawing = false;
    m_activeStroke = Stroke();
    m_activeStrokeLayer = {};
    invalidateActiveStrokePreview();
    m_activeStrokeRenderCache.clipPaths.clear();
    m_strokeStabilizer.reset();
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
    if (!m_panning)
    {
        return;
    }
    m_panning = false;
    updateCursor();
    const QRect pointerRect = pointerUpdateRect();
    if (!pointerRect.isEmpty())
    {
        update(pointerRect);
    }
}

QPointF CanvasWidget::zoomAnchorPosition() const
{
    return m_pointerOverWidget ? m_pointerWidgetPosition
                               : QPointF(width() * 0.5, height() * 0.5);
}

void CanvasWidget::zoomToward(qreal targetZoom, const QPointF &widgetPosition)
{
    const qreal nextZoom = std::clamp(targetZoom, minimumZoom, maximumZoom);
    if (qFuzzyCompare(m_zoom, nextZoom))
    {
        return;
    }
    bool inside = false;
    const QPointF anchor = mapToDocument(widgetPosition, &inside);
    m_zoom = nextZoom;
    if (inside)
    {
        m_pan += widgetPosition - documentTransform().map(anchor);
    }
    notifyZoomChanged();
    update();
}

void CanvasWidget::beginZoomDrag(const QPointF &widgetPosition)
{
    cancelStroke();
    endPan();
    m_zoomDragging = true;
    m_zoomDragStart = widgetPosition;
    m_zoomDragStartZoom = m_zoom;
    m_zoomDragAnchor = mapToDocument(widgetPosition, &m_zoomDragAnchorInside);
    updateCursor();
}

void CanvasWidget::continueZoomDrag(const QPointF &widgetPosition)
{
    if (!m_zoomDragging)
    {
        return;
    }
    const qreal distance = widgetPosition.x() - m_zoomDragStart.x();
    const qreal nextZoom =
        std::clamp(m_zoomDragStartZoom
                       * std::pow(2.0, distance / dragZoomDoublingDistance),
            minimumZoom,
            maximumZoom);
    if (qFuzzyCompare(m_zoom, nextZoom))
    {
        return;
    }
    m_zoom = nextZoom;
    if (m_zoomDragAnchorInside)
    {
        m_pan += m_zoomDragStart - documentTransform().map(m_zoomDragAnchor);
    }
    notifyZoomChanged();
    update();
}

void CanvasWidget::endZoomDrag()
{
    if (!m_zoomDragging)
    {
        return;
    }
    m_zoomDragging = false;
    updateCursor();
    const QRect pointerRect = pointerUpdateRect();
    if (!pointerRect.isEmpty())
    {
        update(pointerRect);
    }
}

bool CanvasWidget::isColorPickableTool() const
{
    return m_tool == Tool::Brush || m_tool == Tool::Eraser
           || m_tool == Tool::Bucket;
}

void CanvasWidget::beginColorPick(const QPointF &widgetPosition)
{
    cancelStroke();
    m_pickingColor = true;
    updateCursor();
    pickColorAt(widgetPosition);
    update();
}

void CanvasWidget::endColorPick()
{
    if (!m_pickingColor)
    {
        return;
    }
    m_pickingColor = false;
    m_colorPickFrame = {};
    m_colorPickFrameIndex = -1;
    updateCursor();
    update();
}

void CanvasWidget::pickColorAt(const QPointF &widgetPosition)
{
    bool inside = false;
    const QPointF documentPosition = mapToDocument(widgetPosition, &inside);
    const QSize documentSize = m_controller->document().size;
    if (!inside || !documentSize.isValid())
    {
        return;
    }
    if (m_colorPickFrame.isNull() || m_colorPickFrameIndex != m_currentFrame)
    {
        Document document = hasPendingSelectionTransform()
                                ? documentWithPendingSelectionTransform()
                                : displayDocument();
        if (!m_wobbleAnimationEnabled)
        {
            document.wobbleAmount = 0.0;
        }
        m_colorPickFrame = RenderEngine::render(document, m_currentFrame);
        m_colorPickFrameIndex = m_currentFrame;
    }
    if (m_colorPickFrame.isNull())
    {
        return;
    }
    const int x = std::clamp(static_cast<int>(documentPosition.x()),
        0,
        m_colorPickFrame.width() - 1);
    const int y = std::clamp(static_cast<int>(documentPosition.y()),
        0,
        m_colorPickFrame.height() - 1);
    QColor color = m_colorPickFrame.pixelColor(x, y);
    if (color.alpha() == 0)
    {
        return;
    }
    setBrushColor(color);
}

void CanvasWidget::updatePointerPosition(const QPointF &widgetPosition)
{
    QRect dirtyRect = pointerUpdateRect();
    bool inside = false;
    const QPointF position = mapToDocument(widgetPosition, &inside);
    m_pointerWidgetPosition = widgetPosition;
    m_pointerInside = inside;
    m_pointerOverWidget = true;
    emit pointerPositionChanged(position, inside);
    if (m_selectionMoveMode)
    {
        updateCursor();
    }
    dirtyRect = dirtyRect.united(pointerUpdateRect());
    if (!dirtyRect.isEmpty())
    {
        update(dirtyRect);
    }
}

QRect CanvasWidget::pointerUpdateRect() const
{
    if (!m_pointerOverWidget || m_panning || m_spacePressed || m_pickingColor
        || (m_tool != Tool::Brush && m_tool != Tool::Eraser
            && !m_tabletPointerEraser))
    {
        return {};
    }
    const qreal toolWidth = m_tabletPointerEraser || m_tool == Tool::Eraser
                                ? m_eraserWidth
                                : m_brushWidth;
    const qreal radius =
        std::max(1.0, toolWidth * std::abs(documentTransform().m11()) * 0.5);
    return QRectF(m_pointerWidgetPosition.x() - radius,
        m_pointerWidgetPosition.y() - radius,
        radius * 2.0,
        radius * 2.0)
        .adjusted(-4.0, -4.0, 4.0, 4.0)
        .toAlignedRect()
        .intersected(rect());
}

void CanvasWidget::updateCursor()
{
    if (m_zoomDragging)
    {
        setCursor(Qt::SizeHorCursor);
        return;
    }
    if (m_pickingColor)
    {
        setCursor(Qt::CrossCursor);
        return;
    }
    if (m_panning)
    {
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (m_spacePressed)
    {
        setCursor(Qt::OpenHandCursor);
        return;
    }
    if (m_selectionMoveMode
        && (m_movingSelection
            || (m_pointerOverWidget
                && selectionContains(mapToDocument(m_pointerWidgetPosition)))))
    {
        setCursor(Qt::SizeAllCursor);
        return;
    }
    if (m_groupSelectionActive
        && (m_tabletPointerEraser || m_tool == Tool::Brush
            || m_tool == Tool::Eraser || m_tool == Tool::Bucket))
    {
        setCursor(Qt::ForbiddenCursor);
        return;
    }
    const bool drawsWithRing = m_tabletPointerEraser || m_tool == Tool::Brush
                               || m_tool == Tool::Eraser;
    setCursor(drawsWithRing ? Qt::BlankCursor : Qt::CrossCursor);
}

void CanvasWidget::notifyZoomChanged()
{
    emit zoomChanged(std::max(1, qRound(std::abs(zoom()) * 100.0)));
}

bool CanvasWidget::selectionContains(const QPointF &documentPosition) const
{
    const QImage &sourceMask = m_selectionTransformSession.active
                                   ? m_selectionTransformSession.sourceMask
                                   : m_selectionMask;
    if (sourceMask.isNull() || !std::isfinite(documentPosition.x())
        || !std::isfinite(documentPosition.y()))
    {
        return false;
    }
    QPointF sourcePosition = documentPosition;
    if (m_selectionTransformSession.active)
    {
        bool invertible = false;
        const QTransform inverse =
            m_selectionTransformSession.transform.inverted(&invertible);
        if (!invertible)
        {
            return false;
        }
        sourcePosition = inverse.map(documentPosition);
    }
    if (!std::isfinite(sourcePosition.x()) || !std::isfinite(sourcePosition.y())
        || sourcePosition.x() < 0.0 || sourcePosition.y() < 0.0
        || sourcePosition.x() >= sourceMask.width()
        || sourcePosition.y() >= sourceMask.height())
    {
        return false;
    }
    const QPoint pixel(static_cast<int>(std::floor(sourcePosition.x())),
        static_cast<int>(std::floor(sourcePosition.y())));
    return sourceMask.constScanLine(pixel.y())[pixel.x()] >= 128;
}

void CanvasWidget::beginAreaSelection(const QPointF &documentPosition)
{
    cancelSelectionTransformForBoundary(
        tr("The pending selection transform was canceled before selecting."));
    m_selectionBeforeArea = currentSelectionState();
    m_hasSelectionBeforeArea = true;
    clearSelection();
    m_areaSelectionActive = true;
    m_areaSelectionAnchor = clampedDocumentPosition(documentPosition);
    m_areaSelectionCurrent = m_areaSelectionAnchor;
    m_areaSelectionPoints.clear();
    m_areaSelectionPoints.append(m_areaSelectionAnchor);
    updateSelectionAnimation();
    update();
}

void CanvasWidget::continueAreaSelection(const QPointF &documentPosition)
{
    if (!m_areaSelectionActive)
    {
        return;
    }
    m_areaSelectionCurrent = clampedDocumentPosition(documentPosition);
    if (m_selectionShape == SelectionShape::Freehand
        && (m_areaSelectionPoints.isEmpty()
            || pointDistance(
                   m_areaSelectionCurrent, m_areaSelectionPoints.constLast())
                   >= 1.0))
    {
        m_areaSelectionPoints.append(m_areaSelectionCurrent);
    }
    update();
}

void CanvasWidget::finishAreaSelection()
{
    if (!m_areaSelectionActive)
    {
        return;
    }
    const bool valid = canFinishAreaSelection();
    const QPainterPath path = areaSelectionPath();
    const SelectionState previousSelection =
        m_hasSelectionBeforeArea ? m_selectionBeforeArea : SelectionState();
    m_areaSelectionActive = false;
    m_areaSelectionPoints.clear();
    m_areaSelectionAnchor = {};
    m_areaSelectionCurrent = {};
    m_selectionBeforeArea = {};
    m_hasSelectionBeforeArea = false;
    if (!valid)
    {
        pushSelectionChange(previousSelection, {}, tr("Deselect"));
        return;
    }

    const QSize size = m_controller->document().size;
    QImage mask(size, QImage::Format_Grayscale8);
    mask.fill(0);
    QPainter painter(&mask);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::white);
    painter.drawPath(path);
    painter.end();
    applySelectionMask(std::move(mask), previousSelection);
}

void CanvasWidget::cancelAreaSelection()
{
    if (!m_areaSelectionActive && m_areaSelectionPoints.isEmpty())
    {
        return;
    }
    m_areaSelectionActive = false;
    m_areaSelectionPoints.clear();
    m_areaSelectionAnchor = {};
    m_areaSelectionCurrent = {};
    m_selectionBeforeArea = {};
    m_hasSelectionBeforeArea = false;
    updateSelectionAnimation();
    update();
}

bool CanvasWidget::canFinishAreaSelection() const
{
    if (m_selectionShape == SelectionShape::Freehand)
    {
        return m_areaSelectionPoints.size() >= 3;
    }
    const QRectF bounds =
        QRectF(m_areaSelectionAnchor, m_areaSelectionCurrent).normalized();
    return bounds.width() >= 1.0 && bounds.height() >= 1.0;
}

QPainterPath CanvasWidget::areaSelectionPath() const
{
    QPainterPath path;
    switch (m_selectionShape)
    {
    case SelectionShape::Freehand:
        if (m_areaSelectionPoints.isEmpty())
        {
            return path;
        }
        path.moveTo(m_areaSelectionPoints.first());
        for (int index = 1; index < m_areaSelectionPoints.size(); ++index)
        {
            path.lineTo(m_areaSelectionPoints.at(index));
        }
        return path;
    case SelectionShape::Rectangle:
        path.addRect(
            QRectF(m_areaSelectionAnchor, m_areaSelectionCurrent).normalized());
        return path;
    case SelectionShape::Ellipse:
        path.addEllipse(
            QRectF(m_areaSelectionAnchor, m_areaSelectionCurrent).normalized());
        return path;
    }
    return path;
}

void CanvasWidget::applySelectionMask(
    QImage mask, const SelectionState &previousSelection)
{
    const SelectionState nextSelection = selectionStateForMask(std::move(mask));
    pushSelectionChange(previousSelection, nextSelection, tr("Select area"));
}

CanvasWidget::SelectionState CanvasWidget::selectionStateForMask(
    QImage mask) const
{
    const Document &document = m_controller->document();
    const Layer *layer = document.layer(document.activeLayerId);
    if (mask.isNull() || !maskHasContent(mask) || !layer)
    {
        return {};
    }

    SelectionState state;
    state.mask = std::move(mask);
    state.layer = document.activeLayerId;
    if (!layer->visible || layer->opacity <= 0.0)
    {
        return state;
    }
    if (m_controller->selectionHasVisibleLayerPixels(
            document.activeLayerId, state.mask, m_currentFrame))
    {
        for (const Stroke &stroke : layer->strokes)
        {
            state.strokes.insert(stroke.id);
        }
    }
    return state;
}

CanvasWidget::SelectionState CanvasWidget::currentSelectionState() const
{
    return {m_selectedStrokes, m_selectionLayer, m_selectionMask};
}

void CanvasWidget::restoreSelectionState(const SelectionState &state)
{
    cancelSelectionMove();
    resetSelectionTransformSession();
    const Document &document = m_controller->document();
    const Layer *layer = document.layer(state.layer);
    if (state.mask.isNull() || !layer || state.mask.size() != document.size)
    {
        clearSelection();
        return;
    }

    m_selectedStrokes.clear();
    if (layer->visible && layer->opacity > 0.0
        && m_controller->selectionHasVisibleLayerPixels(
            state.layer, state.mask, m_currentFrame))
    {
        for (const Stroke &stroke : layer->strokes)
        {
            m_selectedStrokes.insert(stroke.id);
        }
    }
    m_selectionLayer = state.layer;
    m_selectionMask = state.mask;
    m_movingSelection = false;
    setSelectionMoveMode(false);
    rebuildSelectionOutline();
    updateSelectionAnimation();
    notifySelectionTransformAvailability();
    emit interactionMessage(
        m_selectedStrokes.isEmpty()
            ? tr("No content in the selected area.")
            : tr("Selected content. Use the action bar to transform "
                 "or remove it."));
    update();
}

void CanvasWidget::pushSelectionChange(const SelectionState &previousSelection,
    const SelectionState &nextSelection,
    const QString &text)
{
    const bool bothEmpty =
        previousSelection.mask.isNull() && nextSelection.mask.isNull();
    if (bothEmpty)
    {
        restoreSelectionState(nextSelection);
        return;
    }

    m_controller->pushSelectionStateCommand(text,
        previousSelection.layer,
        previousSelection.mask,
        nextSelection.layer,
        nextSelection.mask);
}

void CanvasWidget::computeWandSelection(const QPointF &documentPosition)
{
    cancelSelectionTransformForBoundary(
        tr("The pending selection transform was canceled before selecting."));
    const SelectionState previousSelection = currentSelectionState();
    clearSelection();
    const QSize size = m_controller->document().size;
    const QRectF bounds(QPointF(0.0, 0.0), QSizeF(size));
    if (!bounds.contains(documentPosition))
    {
        pushSelectionChange(previousSelection, {}, tr("Deselect"));
        return;
    }
    const QPoint seed(
        std::clamp(static_cast<int>(documentPosition.x()), 0, size.width() - 1),
        std::clamp(
            static_cast<int>(documentPosition.y()), 0, size.height() - 1));

    QImage referenceImage;
    switch (m_wandReference)
    {
    case WandReference::ActiveLayer:
        referenceImage = renderActiveLayerImage();
        break;
    case WandReference::ReferenceLayers:
        referenceImage = renderReferenceLayersImage();
        break;
    case WandReference::AllVisibleLayers:
        referenceImage = renderAllVisibleLayersImage();
        break;
    }
    if (referenceImage.isNull())
    {
        pushSelectionChange(previousSelection, {}, tr("Deselect"));
        if (m_wandReference == WandReference::ReferenceLayers)
        {
            emit interactionMessage(
                tr("Set a visible paint layer as a reference layer first."));
        }
        return;
    }
    const QImage mask = RenderEngine::fillRegionMask(referenceImage, seed);
    if (mask.isNull())
    {
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
    if (!bounds.contains(documentPosition))
    {
        return;
    }
    if (!layer)
    {
        emit interactionMessage(tr("Add a layer before using this tool."));
        return;
    }
    if (m_groupSelectionActive)
    {
        emit interactionMessage(
            tr("Groups can't be painted on. Select a paint layer to draw."));
        return;
    }
    cancelSelectionTransformForBoundary(
        tr("The pending selection transform was canceled before filling."));
    if (!m_selectionMask.isNull()
        && (m_selectionLayer != document.activeLayerId
            || !selectionContains(documentPosition)))
    {
        emit interactionMessage(
            tr("Click inside the selected area to fill it."));
        return;
    }
    if (!layer->visible)
    {
        emit interactionMessage(
            tr("The active layer is hidden. Make it visible to draw."));
        return;
    }
    if (layer->opacity <= 0.0)
    {
        emit interactionMessage(
            tr("The active layer opacity is 0%. Increase it to draw."));
        return;
    }

    Stroke fillStroke;
    fillStroke.seed = QRandomGenerator::global()->generate64();
    fillStroke.mode = StrokeMode::Fill;
    fillStroke.color = m_brushColor;
    fillStroke.width = std::clamp(m_brushWidth,
        DocumentLimits::minimumStrokeWidth,
        DocumentLimits::maximumStrokeWidth);
    fillStroke.brush = m_brushSettings;
    fillStroke.brush.wobbleScale = m_brushRoughness;
    fillStroke.brush.antialiasing = m_brushAntialiasing;
    if (!m_selectionMask.isNull())
    {
        fillStroke.clipMask = m_selectionMask;
    }
    fillStroke.points.append({clampedDocumentPosition(documentPosition), 1.0});
    commitStroke(document.activeLayerId, std::move(fillStroke));
}

void CanvasWidget::beginSelectionMove(const QPointF &documentPosition)
{
    const Document &document = m_controller->document();
    const Layer *layer = document.layer(m_selectionLayer);
    if (!layer || m_selectionMask.isNull() || !hasTransformableSelection())
    {
        return;
    }
    const bool alreadyActive = hasSelectionTransformSession();
    if (!beginSelectionTransformSession())
    {
        emit interactionMessage(
            tr("The selection transform could not be started."));
        return;
    }
    m_movingSelection = true;
    m_moveStartPosition = documentPosition;
    m_moveBaseTransform = m_selectionTransformSession.transform;
    m_moveStartedTransformSession = !alreadyActive;
    updateSelectionActionBar();
    update();
}

void CanvasWidget::continueSelectionMove(const QPointF &documentPosition)
{
    if (!m_movingSelection)
    {
        return;
    }
    const QRectF baseBounds =
        m_moveBaseTransform.mapRect(m_selectionTransformSession.sourceBounds);
    const QPointF delta = safeSelectionDeltaForBounds(
        documentPosition - m_moveStartPosition, baseBounds);
    QTransform translation;
    translation.translate(delta.x(), delta.y());
    setPendingSelectionTransform(translation * m_moveBaseTransform);
}

void CanvasWidget::commitSelectionMove()
{
    if (!m_movingSelection)
    {
        return;
    }
    m_movingSelection = false;
    if (m_moveStartedTransformSession && !hasPendingSelectionTransform())
    {
        resetSelectionTransformSession();
    }
    m_moveStartedTransformSession = false;
    m_moveBaseTransform = QTransform();
    updateSelectionActionBar();
    updateCursor();
    update();
}

void CanvasWidget::cancelSelectionMove()
{
    if (!m_movingSelection)
    {
        return;
    }
    const bool startedSession = m_moveStartedTransformSession;
    const QTransform baseTransform = m_moveBaseTransform;
    m_movingSelection = false;
    m_moveStartedTransformSession = false;
    m_moveBaseTransform = QTransform();
    if (startedSession)
    {
        resetSelectionTransformSession();
    }
    else if (m_selectionTransformSession.active)
    {
        setPendingSelectionTransform(baseTransform);
    }
    updateSelectionActionBar();
    updateCursor();
    update();
}

bool CanvasWidget::beginSelectionTransformSession()
{
    if (m_selectionTransformSession.active)
    {
        return true;
    }
    if (m_selectionMask.isNull() || m_selectionOutline.isEmpty()
        || m_selectedStrokes.isEmpty()
        || !m_controller->document().layer(m_selectionLayer))
    {
        return false;
    }
    const std::optional<PixelSelectionOp> previewOperation =
        makePixelSelectionOp(m_selectionMask, QTransform(), true, true);
    if (!previewOperation)
    {
        return false;
    }

    FloatingTransformSession session;
    session.active = true;
    session.layer = m_selectionLayer;
    session.strokeIds =
        QVector<QUuid>(m_selectedStrokes.cbegin(), m_selectedStrokes.cend());
    session.sourceMask = m_selectionMask;
    session.sourceOutline = m_selectionOutline;
    session.sourceBounds = QRectF(previewOperation->sourceBounds);
    session.previewOperation = *previewOperation;
    session.transform = QTransform();
    m_selectionTransformSession = std::move(session);
    emit selectionTransformSessionChanged(true, false);
    updateSelectionActionBar();
    update();
    return true;
}

bool CanvasWidget::setPendingSelectionTransform(const QTransform &transform)
{
    if (!m_selectionTransformSession.active
        || !isValidSelectionTransform(transform))
    {
        return false;
    }
    m_selectionTransformSession.transform = transform;
    m_selectionTransformSession.previewOperation.transform = transform;
    m_selectionTransformSession.previewOperation.sampling =
        samplingForSelectionTransform(transform);
    emit selectionTransformSessionChanged(true, !fuzzyIdentity(transform));
    updateSelectionActionBar();
    update();
    return true;
}

bool CanvasWidget::isValidSelectionTransform(const QTransform &transform) const
{
    if (!m_selectionTransformSession.active)
    {
        return false;
    }
    PixelSelectionOp operation = m_selectionTransformSession.previewOperation;
    operation.transform = transform;
    operation.sampling = samplingForSelectionTransform(transform);
    return isValidPixelSelectionOp(operation);
}

void CanvasWidget::resetSelectionTransformSession()
{
    if (!m_selectionTransformSession.active)
    {
        return;
    }
    m_selectionTransformSession = {};
    m_moveBaseTransform = QTransform();
    m_moveStartedTransformSession = false;
    emit selectionTransformSessionChanged(false, false);
    updateSelectionActionBar();
    update();
}

void CanvasWidget::cancelSelectionTransformForBoundary(const QString &message)
{
    if (!hasSelectionTransformSession())
    {
        return;
    }
    cancelSelectionMove();
    resetSelectionTransformSession();
    setSelectionMoveMode(false);
    if (!message.isEmpty())
    {
        emit interactionMessage(message);
    }
}

QPainterPath CanvasWidget::displayedSelectionOutline() const
{
    return m_selectionTransformSession.active
               ? m_selectionTransformSession.transform.map(
                     m_selectionTransformSession.sourceOutline)
               : m_selectionOutline;
}

QRectF CanvasWidget::displayedSelectionBounds() const
{
    return displayedSelectionOutline().boundingRect();
}

QPointF CanvasWidget::safeSelectionDeltaForBounds(
    const QPointF &delta, const QRectF &bounds) const
{
    if (!std::isfinite(delta.x()) || !std::isfinite(delta.y())
        || !bounds.isValid())
    {
        return {};
    }
    const qreal maximum = DocumentLimits::maximumStoredCoordinateMagnitude;
    return QPointF(
        std::clamp(
            delta.x(), -maximum - bounds.left(), maximum - bounds.right()),
        std::clamp(
            delta.y(), -maximum - bounds.top(), maximum - bounds.bottom()));
}

void CanvasWidget::clearSelection()
{
    setSelectionMoveMode(false);
    resetSelectionTransformSession();
    if (m_selectedStrokes.isEmpty() && m_selectionMask.isNull()
        && !m_movingSelection)
    {
        updateSelectionAnimation();
        updateSelectionActionBar();
        return;
    }
    m_selectedStrokes.clear();
    m_selectionMask = QImage();
    m_selectionOutline = QPainterPath();
    m_selectionLayer = {};
    m_movingSelection = false;
    updateSelectionAnimation();
    notifySelectionTransformAvailability();
    updateSelectionActionBar();
    update();
}

void CanvasWidget::pruneSelection()
{
    if (m_selectionMask.isNull())
    {
        return;
    }
    const Document &document = m_controller->document();
    const Layer *layer = document.layer(m_selectionLayer);
    if (!layer || document.size != m_selectionMask.size())
    {
        clearSelection();
        return;
    }

    QSet<QUuid> current;
    if (layer->visible && layer->opacity > 0.0
        && m_controller->selectionHasVisibleLayerPixels(
            m_selectionLayer, m_selectionMask, m_currentFrame))
    {
        for (const Stroke &stroke : layer->strokes)
        {
            current.insert(stroke.id);
        }
    }

    if (current != m_selectedStrokes)
    {
        m_selectedStrokes = std::move(current);
        notifySelectionTransformAvailability();
        update();
    }
}

void CanvasWidget::transformSelectionOverlay(const QUuid &layerId,
    const QVector<QUuid> &strokeIds,
    const QTransform &transform)
{
    cancelSelectionTransformForBoundary();
    if (m_selectionMask.isNull() || m_selectionLayer != layerId)
    {
        return;
    }
    const QSet<QUuid> transformed(strokeIds.cbegin(), strokeIds.cend());
    const bool affectsSelection = std::any_of(m_selectedStrokes.cbegin(),
        m_selectedStrokes.cend(),
        [&transformed](const QUuid &id)
        {
            return transformed.contains(id);
        });
    if (!affectsSelection)
    {
        return;
    }
    const std::optional<PixelSelectionOp> operation =
        makePixelSelectionOp(m_selectionMask, transform, false, true);
    const QImage transformedSelection =
        operation ? transformedSelectionSupport(m_selectionMask,
                        m_selectionMask.size(),
                        transform,
                        operation->sampling)
                  : QImage();
    if (transformedSelection.isNull() || !maskHasContent(transformedSelection))
    {
        clearSelection();
        return;
    }
    m_selectionMask = transformedSelection;
    rebuildSelectionOutline();
    update();
}

void CanvasWidget::handleStrokesDuplicated(const QUuid &layerId,
    const QVector<QUuid> &sourceIds,
    const QVector<QUuid> &duplicateIds,
    const QPointF &delta,
    bool duplicated)
{
    cancelSelectionTransformForBoundary();
    if (m_selectionMask.isNull() || m_selectionLayer != layerId
        || sourceIds.size() != duplicateIds.size())
    {
        return;
    }
    const QVector<QUuid> &fromIds = duplicated ? sourceIds : duplicateIds;
    const QVector<QUuid> &toIds = duplicated ? duplicateIds : sourceIds;
    const QSet<QUuid> from(fromIds.cbegin(), fromIds.cend());
    const bool affectsSelection = std::any_of(m_selectedStrokes.cbegin(),
        m_selectedStrokes.cend(),
        [&from](const QUuid &id)
        {
            return from.contains(id);
        });
    if (!affectsSelection)
    {
        return;
    }
    QSet<QUuid> remapped = m_selectedStrokes;
    for (int index = 0; index < fromIds.size(); ++index)
    {
        if (remapped.remove(fromIds[index]))
        {
            remapped.insert(toIds[index]);
        }
    }
    m_selectedStrokes = std::move(remapped);
    QTransform transform;
    const QPointF appliedDelta = duplicated ? delta : -delta;
    transform.translate(appliedDelta.x(), appliedDelta.y());
    const std::optional<PixelSelectionOp> operation =
        makePixelSelectionOp(m_selectionMask, transform, false, true);
    const QImage transformedSelection =
        operation ? transformedSelectionSupport(m_selectionMask,
                        m_selectionMask.size(),
                        transform,
                        operation->sampling)
                  : QImage();
    if (transformedSelection.isNull() || !maskHasContent(transformedSelection))
    {
        clearSelection();
        return;
    }
    m_selectionMask = transformedSelection;
    rebuildSelectionOutline();
    notifySelectionTransformAvailability();
    update();
}

void CanvasWidget::handleSelectionOverlayTransition(const QUuid &layerId,
    const QVector<QUuid> &fromStrokeIds,
    const QVector<QUuid> &toStrokeIds,
    const QImage &fromMask,
    const QImage &toMask)
{
    cancelSelectionTransformForBoundary();
    if (m_selectionMask.isNull() || m_selectionLayer != layerId
        || fromStrokeIds.isEmpty() || toStrokeIds.isEmpty()
        || fromMask.size() != m_selectionMask.size()
        || toMask.size() != m_selectionMask.size()
        || fromMask.format() != QImage::Format_Grayscale8
        || toMask.format() != QImage::Format_Grayscale8)
    {
        return;
    }
    if (m_selectionMask.cacheKey() != fromMask.cacheKey()
        && m_selectionMask != fromMask)
    {
        return;
    }

    const QSet<QUuid> from(fromStrokeIds.cbegin(), fromStrokeIds.cend());
    const bool affectsSelection = std::any_of(m_selectedStrokes.cbegin(),
        m_selectedStrokes.cend(),
        [&from](const QUuid &id)
        {
            return from.contains(id);
        });
    if (!affectsSelection)
    {
        return;
    }

    for (const QUuid &strokeId : fromStrokeIds)
    {
        m_selectedStrokes.remove(strokeId);
    }
    for (const QUuid &strokeId : toStrokeIds)
    {
        m_selectedStrokes.insert(strokeId);
    }
    m_selectionMask = toMask;
    m_movingSelection = false;
    rebuildSelectionOutline();
    updateSelectionAnimation();
    notifySelectionTransformAvailability();
    updateSelectionActionBar();
    update();
}

void CanvasWidget::handleCanvasResized(const QSize &previousSize,
    const QSize &currentSize,
    const QTransform &transform)
{
    cancelSelectionTransformForBoundary(
        tr("The pending selection transform was canceled before resizing."));
    if (!m_selectionMask.isNull() && m_selectionMask.size() == previousSize)
    {
        const QImage transformedSelection =
            transformedMask(m_selectionMask, currentSize, transform);
        if (transformedSelection.isNull()
            || !maskHasContent(transformedSelection))
        {
            clearSelection();
        }
        else
        {
            m_selectionMask = transformedSelection;
            rebuildSelectionOutline();
        }
    }
    for (QPointF &point : m_areaSelectionPoints)
    {
        point = transform.map(point);
    }
    if (m_areaSelectionActive)
    {
        m_areaSelectionAnchor = transform.map(m_areaSelectionAnchor);
        m_areaSelectionCurrent = transform.map(m_areaSelectionCurrent);
    }
    setSelectionMoveMode(false);
    fitToWindow();
}

void CanvasWidget::rebuildSelectionOutline()
{
    m_selectionOutline = outlinePath(m_selectionMask);
}

void CanvasWidget::updateSelectionAnimation()
{
    const bool active = m_areaSelectionActive || !m_selectionMask.isNull();
    if (active && !m_selectionAnimationTimer.isActive())
    {
        m_selectionAnimationTimer.start();
    }
    else if (!active && m_selectionAnimationTimer.isActive())
    {
        m_selectionAnimationTimer.stop();
        m_selectionDashOffset = 0.0;
    }
}

void CanvasWidget::notifySelectionTransformAvailability()
{
    emit selectionTransformAvailabilityChanged(hasTransformableSelection());
    emit selectionAvailabilityChanged(
        hasSelection(), hasTransformableSelection());
    updateSelectionActionBar();
}

bool CanvasWidget::flipSelection(bool horizontal)
{
    if (!hasTransformableSelection())
    {
        return false;
    }
    setSelectionMoveMode(false);
    const bool alreadyActive = hasSelectionTransformSession();
    if (!beginSelectionTransformSession())
    {
        return false;
    }
    const QPointF center = displayedSelectionBounds().center();
    QTransform delta;
    delta.translate(center.x(), center.y());
    delta.scale(horizontal ? -1.0 : 1.0, horizontal ? 1.0 : -1.0);
    delta.translate(-center.x(), -center.y());
    if (!setPendingSelectionTransform(
            delta * m_selectionTransformSession.transform))
    {
        if (!alreadyActive)
        {
            resetSelectionTransformSession();
        }
        emit interactionMessage(
            tr("The selected content could not be flipped."));
        return false;
    }
    return true;
}

void CanvasWidget::updateSelectionActionBar()
{
    if (!m_selectionActionBar)
    {
        return;
    }
    if (m_selectionMask.isNull() || m_selectionOutline.isEmpty()
        || m_areaSelectionActive || m_movingSelection)
    {
        m_selectionActionBar->hide();
        return;
    }

    m_selectionActionBar->adjustSize();
    const QRectF documentBounds = displayedSelectionBounds();
    const QRectF widgetBounds =
        documentTransform().mapRect(documentBounds).normalized();
    const QSize barSize = m_selectionActionBar->size();
    constexpr int edgeMargin = 8;
    constexpr int selectionGap = 4;
    const int x = barSize.width() + 2 * edgeMargin <= width()
                      ? std::clamp(qRound(widgetBounds.center().x()
                                          - barSize.width() * 0.5),
                            edgeMargin,
                            width() - barSize.width() - edgeMargin)
                      : (width() - barSize.width()) / 2;
    int y = qCeil(widgetBounds.bottom()) + selectionGap;
    if (y + barSize.height() > height() - edgeMargin)
    {
        y = qFloor(widgetBounds.top()) - barSize.height() - selectionGap;
    }
    y = barSize.height() + 2 * edgeMargin <= height()
            ? std::clamp(
                  y, edgeMargin, height() - barSize.height() - edgeMargin)
            : (height() - barSize.height()) / 2;
    m_selectionActionBar->move(x, y);
    m_selectionActionBar->show();
    m_selectionActionBar->raise();
}

QPointF CanvasWidget::clampedSelectionDelta(const QPointF &delta) const
{
    const Document &document = m_controller->document();
    const Layer *layer = document.layer(m_selectionLayer);
    if (!layer || m_selectedStrokes.isEmpty())
    {
        return delta;
    }
    if (!m_selectionMask.isNull() && !m_selectionOutline.isEmpty())
    {
        const QRectF bounds = m_selectionOutline.boundingRect();
        return QPointF(
            std::clamp(delta.x(),
                -bounds.left(),
                static_cast<qreal>(document.size.width()) - bounds.right()),
            std::clamp(delta.y(),
                -bounds.top(),
                static_cast<qreal>(document.size.height()) - bounds.bottom()));
    }
    qreal minX = document.size.width();
    qreal minY = document.size.height();
    qreal maxX = 0.0;
    qreal maxY = 0.0;
    bool found = false;
    for (const Stroke &stroke : layer->strokes)
    {
        if (!m_selectedStrokes.contains(stroke.id))
        {
            continue;
        }
        for (const StrokePoint &point : stroke.points)
        {
            minX = std::min(minX, point.position.x());
            minY = std::min(minY, point.position.y());
            maxX = std::max(maxX, point.position.x());
            maxY = std::max(maxY, point.position.y());
            found = true;
        }
    }
    if (!found)
    {
        return delta;
    }
    return QPointF(
        std::clamp(
            delta.x(), -minX, static_cast<qreal>(document.size.width()) - maxX),
        std::clamp(delta.y(),
            -minY,
            static_cast<qreal>(document.size.height()) - maxY));
}

QImage CanvasWidget::renderActiveLayerImage() const
{
    const Document document = displayDocument();
    const Layer *layer = document.layer(document.activeLayerId);
    if (!layer)
    {
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

QImage CanvasWidget::renderReferenceLayersImage() const
{
    Document document = displayDocument();
    if (!document.size.isValid())
    {
        return {};
    }
    bool hasVisibleReference = false;
    for (Layer &layer : document.layers)
    {
        if (layer.kind != LayerKind::Paint)
        {
            continue;
        }
        if (!layer.reference)
        {
            layer.visible = false;
            continue;
        }
        bool visible = layer.visible && layer.opacity > 0.0;
        QUuid parentId = layer.parentGroupId;
        for (int depth = 0;
            !parentId.isNull() && depth < document.layers.size();
            ++depth)
        {
            const Layer *parent = document.layer(parentId);
            if (!parent || !parent->visible || parent->opacity <= 0.0)
            {
                visible = false;
                break;
            }
            parentId = parent->parentGroupId;
        }
        hasVisibleReference = hasVisibleReference || visible;
    }
    if (!hasVisibleReference)
    {
        return {};
    }
    document.background = Qt::transparent;
    return RenderEngine::render(document, m_currentFrame);
}

QImage CanvasWidget::renderAllVisibleLayersImage() const
{
    Document document = displayDocument();
    document.background = Qt::transparent;
    return RenderEngine::render(document, m_currentFrame);
}

void CanvasWidget::drawSelectionOverlay(
    QPainter &painter, const QTransform &transform)
{
    painter.save();
    painter.setTransform(transform);

    if (m_areaSelectionActive)
    {
        const QPainterPath path = areaSelectionPath();
        if (!path.isEmpty())
        {
            drawSelectionPath(painter, path, m_selectionDashOffset);
        }
    }

    if (!m_selectionOutline.isEmpty())
    {
        drawSelectionPath(
            painter, displayedSelectionOutline(), m_selectionDashOffset);
    }
    painter.restore();
}

}
