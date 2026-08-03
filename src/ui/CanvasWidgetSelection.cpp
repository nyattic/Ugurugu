#include "document/DocumentLimits.hpp"
#include "document/SelectionOperation.hpp"
#include "document/SelectionVisibility.hpp"
#include "document/StrokeMask.hpp"
#include "render/RenderEngine.hpp"
#include "ui/CanvasViewport.hpp"
#include "ui/CanvasWidget.hpp"
#include "ui/SelectionActionBar.hpp"
#include "ui/Theme.hpp"

#include <QFutureWatcher>
#include <QHash>
#include <QPainter>
#include <QPointer>
#include <QRandomGenerator>
#include <QtConcurrentRun>

#include <algorithm>
#include <cmath>

namespace wobble
{

using namespace canvas_detail;

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
    m_selectionLayer = state.layer;
    m_selectionMask = state.mask;
    m_movingSelection = false;
    setSelectionMoveMode(false);
    rebuildSelectionOutline();
    updateSelectionAnimation();
    notifySelectionTransformAvailability();
    update();
    evaluateSelectionVisibility();
}

void CanvasWidget::evaluateSelectionVisibility()
{
    const quint64 generation = ++m_selectionVisibilityGeneration;
    const Document document = m_controller->document();
    const QUuid layerId = m_selectionLayer;
    const QImage mask = m_selectionMask;
    const qint64 maskKey = mask.cacheKey();
    const int frame = m_currentFrame;
    const Layer *layer = document.layer(layerId);
    if (!layer || !layer->visible || layer->opacity <= 0.0 || mask.isNull())
    {
        m_selectedStrokes.clear();
        if (layer && !mask.isNull())
        {
            m_controller->cacheSelectionVisibility(layerId, mask, false);
        }
        notifySelectionTransformAvailability();
        emit interactionMessage(tr("No content in the selected area."));
        update();
        return;
    }

    auto *watcher = new QFutureWatcher<SelectionVisibility::Result>(this);
    connect(watcher,
        &QFutureWatcher<SelectionVisibility::Result>::finished,
        this,
        [this, watcher, generation, layerId, maskKey]()
        {
            const SelectionVisibility::Result result = watcher->result();
            watcher->deleteLater();
            if (generation != m_selectionVisibilityGeneration
                || layerId != m_selectionLayer
                || maskKey != m_selectionMask.cacheKey())
            {
                return;
            }

            QSet<QUuid> selected;
            const Layer *currentLayer =
                m_controller->document().layer(m_selectionLayer);
            if (result.renderSucceeded && result.hasVisiblePixels
                && currentLayer)
            {
                for (const Stroke &stroke : currentLayer->strokes)
                {
                    selected.insert(stroke.id);
                }
            }
            if (result.renderSucceeded)
            {
                m_controller->cacheSelectionVisibility(
                    m_selectionLayer, m_selectionMask, result.hasVisiblePixels);
            }
            m_selectedStrokes = std::move(selected);
            notifySelectionTransformAvailability();
            emit interactionMessage(
                m_selectedStrokes.isEmpty()
                    ? tr("No content in the selected area.")
                    : tr("Selected content. Use the action bar to transform "
                         "or remove it."));
            update();
        });
    watcher->setFuture(QtConcurrent::run(
        [document, layerId, mask, frame]()
        {
            const Layer *snapshotLayer = document.layer(layerId);
            return snapshotLayer ? SelectionVisibility::evaluate(
                                       document, *snapshotLayer, mask, frame)
                                 : SelectionVisibility::Result();
        }));
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
    ++m_selectionVisibilityGeneration;
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
    m_selectionLayer = QUuid();
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

    m_selectedStrokes.clear();
    notifySelectionTransformAvailability();
    update();
    evaluateSelectionVisibility();
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
