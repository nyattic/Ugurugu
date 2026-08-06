#include "app/WatchedFutureResult.hpp"
#include "document/DocumentLimits.hpp"
#include "document/SelectionOperation.hpp"
#include "render/PreviewRenderPolicy.hpp"
#include "render/RenderEngine.hpp"
#include "ui/CanvasViewport.hpp"
#include "ui/CanvasWidget.hpp"

#include <QFutureWatcher>
#include <QPainter>
#include <QtConcurrentRun>

#include <algorithm>
#include <cmath>

namespace ugurugu
{

using namespace canvas_detail;

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
        resetFrameCacheStorage();
        m_cachedRenderSize = renderSize;
    }
    const bool stale = m_frameCacheStaleFrames.contains(frame);
    if (!stale)
    {
        if (QImage *cached = m_frameCache.object(frame))
        {
            return *cached;
        }
    }
    QImage image;
    if (stale && m_frameCacheRefreshDocument
        && !m_frameCacheRefreshOutputBounds.isEmpty())
    {
        // A stale frame only misrenders inside the pending refresh bounds, so
        // rendering the filtered document and patching that region is exact
        // and far cheaper than a full render.
        if (QImage *base = m_frameCache.object(frame);
            base && base->size() == renderSize)
        {
            const QImage regional = RenderEngine::renderScaled(
                *m_frameCacheRefreshDocument, frame, renderSize);
            if (regional.size() == base->size())
            {
                image = base->copy();
                QPainter painter(&image);
                painter.setCompositionMode(QPainter::CompositionMode_Source);
                painter.drawImage(m_frameCacheRefreshOutputBounds.topLeft(),
                    regional,
                    m_frameCacheRefreshOutputBounds);
                painter.end();
            }
        }
    }
    if (image.isNull() && m_previewSplit.valid && m_previewSplitFrame == frame
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
    m_frameCacheStaleFrames.remove(frame);
    clearCompletedFrameCacheRefresh();
    return image;
}

QImage CanvasWidget::activeStrokePreview(
    const Document &document, const QSize &renderSize, bool &resolved)
{
    if (m_activeStrokePreviewResolved
        && m_activeStrokePreviewRenderSize == renderSize
        && m_activeStrokePreviewFrame == m_currentFrame)
    {
        resolved = true;
        return m_activeStrokePreview;
    }

    QImage preview;
    QRect patchBounds;
    bool patchBoundsValid = false;
    if (const Layer *strokeLayer = document.layer(m_activeStrokeLayer))
    {
        // The live stroke has to wobble the way its own layer does, not the
        // way the document does.
        const Document strokeDocument =
            documentForLayer(document, *strokeLayer);
        const RenderEngine::LayerSplitFrame &split =
            previewSplit(m_activeStrokeLayer, renderSize);
        if (split.valid)
        {
            const IncrementalStrokeRenderer::Update update =
                m_incrementalStrokeRenderer.update(split.layerBase,
                    strokeDocument,
                    m_activeStroke,
                    m_currentFrame,
                    renderSize);
            const QImage baseFrame = frameImage(m_currentFrame);
            if (update.valid && !baseFrame.isNull())
            {
                bool rebuiltComposedBase = false;
                if (m_composedPreviewFrame.size() != baseFrame.size()
                    || m_composedPreviewBaseKey != baseFrame.cacheKey())
                {
                    m_composedPreviewFrame = baseFrame.copy();
                    m_composedPreviewBaseKey = baseFrame.cacheKey();
                    rebuiltComposedBase = true;
                }
                QPainter painter(&m_composedPreviewFrame);
                painter.setCompositionMode(QPainter::CompositionMode_Source);
                bool composedAllPatches = true;
                QRect composedBounds;
                for (const IncrementalStrokeRenderer::Patch &patch :
                    update.patches)
                {
                    const QImage composed =
                        RenderEngine::composeLayerSplitRegion(
                            split, patch.layerImage, patch.bounds);
                    if (composed.isNull())
                    {
                        composedAllPatches = false;
                        break;
                    }
                    painter.drawImage(patch.bounds.topLeft(), composed);
                    composedBounds = composedBounds.united(patch.bounds);
                }
                painter.end();
                if (composedAllPatches)
                {
                    preview = m_composedPreviewFrame;
                    patchBounds = rebuiltComposedBase
                                      ? QRect(QPoint(), renderSize)
                                      : composedBounds;
                    patchBoundsValid = true;
                }
                else
                {
                    m_composedPreviewFrame = baseFrame.copy();
                    m_composedPreviewBaseKey = baseFrame.cacheKey();
                }
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
                const IncrementalStrokeRenderer::Update update =
                    m_incrementalStrokeRenderer.update(cached.value(),
                        strokeDocument,
                        m_activeStroke,
                        m_currentFrame,
                        renderSize);
                const QImage baseFrame = frameImage(m_currentFrame);
                if (update.valid && !baseFrame.isNull())
                {
                    bool rebuiltComposedBase = false;
                    if (m_composedPreviewFrame.size() != baseFrame.size()
                        || m_composedPreviewBaseKey != baseFrame.cacheKey())
                    {
                        m_composedPreviewFrame = baseFrame.copy();
                        m_composedPreviewBaseKey = baseFrame.cacheKey();
                        rebuiltComposedBase = true;
                    }
                    QPainter painter(&m_composedPreviewFrame);
                    painter.setCompositionMode(
                        QPainter::CompositionMode_Source);
                    bool composedAllPatches = true;
                    QRect composedBounds;
                    for (const IncrementalStrokeRenderer::Patch &patch :
                        update.patches)
                    {
                        const QImage composed =
                            RenderEngine::composeLayerRasterFrameRegion(
                                document,
                                rasters,
                                m_activeStrokeLayer,
                                patch.layerImage,
                                patch.bounds);
                        if (composed.isNull())
                        {
                            composedAllPatches = false;
                            break;
                        }
                        painter.drawImage(patch.bounds.topLeft(), composed);
                        composedBounds = composedBounds.united(patch.bounds);
                    }
                    painter.end();
                    if (composedAllPatches)
                    {
                        preview = m_composedPreviewFrame;
                        patchBounds = rebuiltComposedBase
                                          ? QRect(QPoint(), renderSize)
                                          : composedBounds;
                        patchBoundsValid = true;
                    }
                    else
                    {
                        m_composedPreviewFrame = baseFrame.copy();
                        m_composedPreviewBaseKey = baseFrame.cacheKey();
                    }
                }
            }
        }
    }
    if (preview.isNull())
    {
        preview = interactionPreview(document, renderSize);
        patchBounds = {};
        patchBoundsValid = false;
    }
    m_activeStrokePreview = preview;
    m_activeStrokePreviewRenderSize = renderSize;
    m_activeStrokePreviewFrame = m_currentFrame;
    m_activeStrokePreviewResolved = !preview.isNull();
    m_activeStrokePreviewPatchBounds = patchBounds;
    m_activeStrokePreviewPatchBoundsValid =
        patchBoundsValid && m_activeStrokePreviewResolved;
    if (m_activeStrokePreviewPatchBoundsValid)
    {
        m_displayedFrameDirtyAccum |= patchBounds;
        m_displayedFramePatchedKey = preview.cacheKey();
    }
    resolved = m_activeStrokePreviewResolved;
    updateFrameCacheBudget();
    return preview;
}

void CanvasWidget::invalidateActiveStrokePreview()
{
    m_activeStrokePreview = {};
    m_activeStrokePreviewRenderSize = {};
    m_activeStrokePreviewFrame = -1;
    m_activeStrokePreviewResolved = false;
    m_activeStrokePreviewPatchBounds = {};
    m_activeStrokePreviewPatchBoundsValid = false;
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

CanvasWidget::DisplayedFrame CanvasWidget::resolveDisplayedFrame()
{
    const Document document = displayDocument();
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
            m_displayedFrameDirtyAccum |= resetRegion;
            m_displayedFramePatchedKey = m_composedPreviewFrame.cacheKey();
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

    DisplayedFrame result;
    result.image = displayedFrame;
    const QRect fullBounds(QPoint(), displayedFrame.size());
    const qint64 key = displayedFrame.cacheKey();
    if (displayedFrame.size() == m_displayedFrameSize
        && (key == m_displayedFrameKey || key == m_displayedFramePatchedKey))
    {
        result.dirtyBounds = m_displayedFrameDirtyAccum.intersected(fullBounds);
    }
    else
    {
        result.dirtyBounds = fullBounds;
    }
    m_displayedFrameKey = key;
    m_displayedFramePatchedKey = 0;
    m_displayedFrameSize = displayedFrame.size();
    m_displayedFrameDirtyAccum = {};
    return result;
}

const RenderEngine::LayerSplitFrame &CanvasWidget::previewSplit(
    const QUuid &layerId, const QSize &renderSize)
{
    if (!m_previewSplit.valid || m_previewSplitLayer != layerId
        || m_previewSplitFrame != m_currentFrame
        || m_previewSplit.below.size() != renderSize)
    {
        m_previewSplit = {};
        m_previewSplit = RenderEngine::renderLayerSplit(
            displayDocument(), m_currentFrame, renderSize, layerId);
        m_previewSplitLayer = layerId;
        m_previewSplitFrame = m_currentFrame;
        updateFrameCacheBudget();
    }
    return m_previewSplit;
}

const RenderEngine::LayerRasterFrame &CanvasWidget::previewLayerRasters(
    const QSize &renderSize)
{
    if (m_previewLayerRasterFrame != m_currentFrame
        || m_previewLayerRasters.outputSize != renderSize)
    {
        resetFrameCacheStorage();
        m_previewLayerRasters = {};
        const qint64 budgetBytes =
            static_cast<qint64>(PreviewRenderPolicy::maximumCacheKiB()) * 1024;
        m_previewLayerRasters =
            RenderEngine::renderLayerRasterFrame(displayDocument(),
                m_currentFrame,
                renderSize,
                budgetBytes - previewSurfaceUsage().pinnedBytes());
        m_previewLayerRasterFrame = m_currentFrame;
        updateFrameCacheBudget();
    }
    return m_previewLayerRasters;
}

QSize CanvasWidget::previewRenderSize() const
{
    if (m_zoomRenderTimer.isActive() && m_cachedRenderSize.isValid())
    {
        return m_cachedRenderSize;
    }
    const Document &document = m_controller->document();
    const QSize documentSize = document.size;
    const qreal displayScale =
        std::abs(documentTransform().m11()) * devicePixelRatioF();
    // Sized for the whole frame cache whether or not playback is running, for
    // the same reason the active stroke below is always reserved for: sizing
    // off m_animating changed the render size on every play and pause, and
    // each change threw away every cached frame. Resuming then re-rendered the
    // entire animation before the first frame could be shown.
    int retainedSurfaces = std::max(1, document.animationFrames);
    // Sized for an active stroke whether or not one is in progress. Reserving
    // only while drawing would change the render size the moment a stroke
    // starts, and that discards every preview cache mid-interaction.
    const QUuid strokeLayerId =
        m_drawing ? m_activeStrokeLayer : document.activeLayerId;
    if (RenderEngine::supportsLayerSplit(document, strokeLayerId))
    {
        retainedSurfaces += PreviewRenderPolicy::activeStrokeSurfaceCount;
    }
    else
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
            std::max(retainedSurfaces, paintSurfaces + (hasEmptyLayer ? 1 : 0))
            + 1;
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

PreviewSurfaceUsage CanvasWidget::previewSurfaceUsage() const
{
    PreviewSurfaceUsage usage;
    usage.frameCacheBytes =
        static_cast<qint64>(m_frameCache.totalCost()) * 1024;
    if (m_previewSplit.valid)
    {
        usage.layerSplitBytes = m_previewSplit.below.sizeInBytes()
                                + m_previewSplit.layerBase.sizeInBytes()
                                + m_previewSplit.above.sizeInBytes();
    }
    for (auto layer = m_previewLayerRasters.paintLayers.cbegin();
        layer != m_previewLayerRasters.paintLayers.cend();
        ++layer)
    {
        usage.layerRasterBytes += layer.value().sizeInBytes();
    }
    usage.composedPreviewBytes = m_composedPreviewFrame.sizeInBytes();
    // The resolved stroke preview usually is the composed frame rather than a
    // copy of it, so its bytes may only be added when the two do not share one
    // backing store.
    if (!m_activeStrokePreview.isNull()
        && m_activeStrokePreview.cacheKey()
               != m_composedPreviewFrame.cacheKey())
    {
        usage.composedPreviewBytes += m_activeStrokePreview.sizeInBytes();
    }
    usage.colorPickBytes = m_colorPickFrame.sizeInBytes();
    usage.strokeTileBytes =
        static_cast<qint64>(m_incrementalStrokeRenderer.cachedTileBytes());
    return usage;
}

void CanvasWidget::updateFrameCacheBudget()
{
    const qint64 frameBytes =
        m_cachedRenderSize.isValid()
            ? static_cast<qint64>(m_cachedRenderSize.width())
                  * m_cachedRenderSize.height() * 4
            : 0;
    m_frameCache.setMaxCost(PreviewRenderPolicy::frameCacheCostKiB(
        previewSurfaceUsage().pinnedBytes(), frameBytes));
}

void CanvasWidget::resetFrameCacheStorage()
{
    m_frameCache.clear();
    m_frameCacheStaleFrames.clear();
    m_frameCacheRefreshNativeBounds = {};
    m_frameCacheRefreshOutputBounds = {};
    m_frameCacheRefreshDocument.reset();
}

void CanvasWidget::clearCompletedFrameCacheRefresh()
{
    if (!m_frameCacheStaleFrames.isEmpty())
    {
        return;
    }
    m_frameCacheRefreshNativeBounds = {};
    m_frameCacheRefreshOutputBounds = {};
    m_frameCacheRefreshDocument.reset();
}

bool CanvasWidget::tryRegionalStrokeInvalidation(
    const QUuid &layerId, const QUuid &strokeId)
{
    const QSize renderSize = previewRenderSize();
    if (renderSize.isEmpty() || renderSize != m_cachedRenderSize
        || m_frameCache.isEmpty())
    {
        return false;
    }
    // Frames still waiting on an earlier refresh are missing those pixels
    // too, so the new refresh has to cover the union of both regions.
    if (!m_frameCacheStaleFrames.isEmpty()
        && m_frameCacheRefreshNativeBounds.isEmpty())
    {
        return false;
    }
    RenderEngine::RegionalStrokeRefresh refresh =
        RenderEngine::prepareRegionalStrokeRefresh(displayDocument(),
            layerId,
            strokeId,
            renderSize,
            m_frameCacheStaleFrames.isEmpty()
                ? QRect()
                : m_frameCacheRefreshNativeBounds);
    if (!refresh.valid)
    {
        return false;
    }

    // Mirrors invalidateFrames() except that the frame cache itself survives
    // as the base the regional patches draw over.
    cancelFrameCacheWarmup();
    m_colorPickFrame = {};
    m_colorPickFrameIndex = -1;
    m_previewSplit = {};
    m_previewSplitLayer = QUuid();
    m_previewSplitFrame = -1;
    m_previewLayerRasters = {};
    m_previewLayerRasterFrame = -1;
    m_composedPreviewFrame = {};
    m_composedSelectionPreviewRegion = {};
    m_composedPreviewBaseKey = 0;
    invalidateActiveStrokePreview();
    m_incrementalStrokeRenderer.clear();
    m_editableStrokeIds.clear();
    m_editableStrokeMaskKey = 0;
    m_editableStrokeLayer = QUuid();
    m_editableStrokeFrame = -1;

    m_frameCacheRefreshNativeBounds = refresh.nativeBounds;
    m_frameCacheRefreshOutputBounds = refresh.outputBounds;
    m_frameCacheRefreshDocument =
        std::make_shared<const Document>(std::move(refresh.filteredDocument));
    const QList<int> cachedFrames = m_frameCache.keys();
    m_frameCacheStaleFrames =
        QSet<int>(cachedFrames.cbegin(), cachedFrames.cend());

    updateFrameCacheBudget();
    updateTimerInterval();
    notifyZoomChanged();
    requestDisplayUpdate();
    scheduleFrameCacheWarmup();
    return true;
}

void CanvasWidget::invalidateFrames()
{
    cancelFrameCacheWarmup();
    resetFrameCacheStorage();
    m_cachedRenderSize = {};
    m_colorPickFrame = {};
    m_colorPickFrameIndex = -1;
    m_previewSplit = {};
    m_previewSplitLayer = QUuid();
    m_previewSplitFrame = -1;
    m_previewLayerRasters = {};
    m_previewLayerRasterFrame = -1;
    m_composedPreviewFrame = {};
    m_composedSelectionPreviewRegion = {};
    m_composedPreviewBaseKey = 0;
    invalidateActiveStrokePreview();
    m_incrementalStrokeRenderer.clear();
    m_editableStrokeIds.clear();
    m_editableStrokeMaskKey = 0;
    m_editableStrokeLayer = QUuid();
    m_editableStrokeFrame = -1;
    const int frames = std::max(1, m_controller->document().animationFrames);
    if (const int normalized = m_currentFrame % frames;
        normalized != m_currentFrame)
    {
        // Consumers cache the frame they were last told about. Normalizing in
        // silence left them correct only because the canvas happens to be the
        // first documentChanged connection, so everyone else re-read the
        // document after this had already run.
        m_currentFrame = normalized;
        emit currentFrameChanged(m_currentFrame);
    }
    updateFrameCacheBudget();
    updateTimerInterval();
    notifyZoomChanged();
    requestDisplayUpdate();
    scheduleFrameCacheWarmup();
}

void CanvasWidget::cancelFrameCacheWarmup()
{
    if (m_frameCacheWarmupCancellation)
    {
        m_frameCacheWarmupCancellation->store(true, std::memory_order_relaxed);
    }
    ++m_frameCacheWarmupGeneration;
    m_frameCacheWarmupActive = false;
    m_frameCacheWarmupCancellation.reset();
    m_frameCacheWarmupDocument.reset();
    m_frameCacheWarmupFrames.clear();
    m_frameCacheWarmupRenderSize = {};
    m_frameCacheWarmupPatchBounds = {};
    m_frameCacheWarmupCursor = 0;
}

void CanvasWidget::scheduleFrameCacheWarmup()
{
    const quint64 generation = m_frameCacheWarmupGeneration;
    QTimer::singleShot(0,
        this,
        [this, generation]()
        {
            // Warmed while paused too: every edit clears the cache, so gating
            // this on playback left the cache empty for the whole time the
            // user was drawing and made the next play re-render every frame on
            // the GUI thread as playback reached it.
            if (generation != m_frameCacheWarmupGeneration
                || !m_wobbleAnimationEnabled
                || (m_drawing && !m_animateWhileDrawing))
            {
                return;
            }

            const QSize renderSize = previewRenderSize();
            const int frameCount =
                std::max(1, m_controller->document().animationFrames);
            if (renderSize.isEmpty() || frameCount <= 1)
            {
                return;
            }
            if (m_cachedRenderSize != renderSize)
            {
                resetFrameCacheStorage();
                m_cachedRenderSize = renderSize;
            }

            QVector<int> missingFrames;
            QVector<int> staleFrames;
            missingFrames.reserve(frameCount);
            for (int offset = 0; offset < frameCount; ++offset)
            {
                const int frame = (m_currentFrame + offset) % frameCount;
                if (!m_frameCache.object(frame))
                {
                    missingFrames.append(frame);
                }
                else if (m_frameCacheStaleFrames.contains(frame))
                {
                    staleFrames.append(frame);
                }
            }
            // Stale frames patch far faster than missing frames render, so
            // they refresh first; the finish path reschedules for whatever
            // remains.
            const bool patchMode = !staleFrames.isEmpty()
                                   && m_frameCacheRefreshDocument
                                   && !m_frameCacheRefreshOutputBounds.isEmpty();
            if (!patchMode && missingFrames.isEmpty())
            {
                return;
            }

            m_frameCacheWarmupDocument =
                patchMode ? m_frameCacheRefreshDocument
                          : std::make_shared<const Document>(displayDocument());
            m_frameCacheWarmupCancellation =
                std::make_shared<std::atomic_bool>(false);
            m_frameCacheWarmupFrames =
                patchMode ? std::move(staleFrames) : std::move(missingFrames);
            m_frameCacheWarmupPatchBounds =
                patchMode ? m_frameCacheRefreshOutputBounds : QRect();
            m_frameCacheWarmupRenderSize = renderSize;
            m_frameCacheWarmupCursor = 0;
            m_frameCacheWarmupActive = true;
            renderNextFrameCacheWarmup();
        });
}

void CanvasWidget::renderNextFrameCacheWarmup()
{
    if (!m_frameCacheWarmupActive || !m_frameCacheWarmupDocument
        || !m_frameCacheWarmupCancellation
        || m_frameCacheWarmupCursor >= m_frameCacheWarmupFrames.size())
    {
        // Workers from a cancelled or exhausted warmup may still be running;
        // the last one to finish performs the reset.
        if (m_frameCacheWarmupWorkersRunning > 0)
        {
            return;
        }
        const bool finishedRun = m_frameCacheWarmupActive;
        m_frameCacheWarmupActive = false;
        m_frameCacheWarmupCancellation.reset();
        m_frameCacheWarmupDocument.reset();
        m_frameCacheWarmupFrames.clear();
        m_frameCacheWarmupRenderSize = {};
        m_frameCacheWarmupPatchBounds = {};
        m_frameCacheWarmupCursor = 0;
        requestDisplayUpdate();
        if (finishedRun)
        {
            // A patch run may leave whole frames still missing (or vice
            // versa); a fresh schedule picks up whatever remains and returns
            // without work otherwise.
            scheduleFrameCacheWarmup();
        }
        return;
    }

    // Frames render on every pool thread at once. Wobble gives each frame
    // unique geometry, so frames cannot share work, which makes them ideal to
    // parallelise; dispatch stays in playback order so playback can step onto
    // finished frames while later ones are still rendering.
    while (m_frameCacheWarmupWorkersRunning
               < m_frameCacheWarmupPool.maxThreadCount()
        && m_frameCacheWarmupCursor < m_frameCacheWarmupFrames.size())
    {
        const quint64 generation = m_frameCacheWarmupGeneration;
        const int frame = m_frameCacheWarmupFrames[m_frameCacheWarmupCursor];
        ++m_frameCacheWarmupCursor;
        const QSize renderSize = m_frameCacheWarmupRenderSize;
        const QRect patchBounds = m_frameCacheWarmupPatchBounds;
        const std::shared_ptr<const Document> document =
            m_frameCacheWarmupDocument;
        const std::shared_ptr<std::atomic_bool> cancellation =
            m_frameCacheWarmupCancellation;
        ++m_frameCacheWarmupWorkersRunning;
        auto *watcher = new QFutureWatcher<QImage>(this);
        connect(watcher,
            &QFutureWatcher<QImage>::finished,
            this,
            [this, watcher, generation, frame, renderSize, patchBounds,
                cancellation]()
            {
                const QImage image = watchedFutureResult(*watcher);
                watcher->deleteLater();
                --m_frameCacheWarmupWorkersRunning;
                if (generation != m_frameCacheWarmupGeneration
                    || cancellation != m_frameCacheWarmupCancellation
                    || cancellation->load(std::memory_order_relaxed))
                {
                    QTimer::singleShot(
                        0, this, &CanvasWidget::renderNextFrameCacheWarmup);
                    return;
                }
                if (previewRenderSize() != renderSize)
                {
                    cancelFrameCacheWarmup();
                    QTimer::singleShot(
                        0, this, &CanvasWidget::renderNextFrameCacheWarmup);
                    return;
                }
                if (!patchBounds.isEmpty())
                {
                    QImage *base = m_frameCache.object(frame);
                    if (!image.isNull() && base && base->size() == image.size()
                        && m_frameCacheStaleFrames.contains(frame))
                    {
                        QImage patched = base->copy();
                        QPainter painter(&patched);
                        painter.setCompositionMode(
                            QPainter::CompositionMode_Source);
                        painter.drawImage(
                            patchBounds.topLeft(), image, patchBounds);
                        painter.end();
                        const int cost = PreviewRenderPolicy::cacheCostKiB(
                            patched.sizeInBytes());
                        m_frameCache.insert(frame, new QImage(patched), cost);
                        m_frameCacheStaleFrames.remove(frame);
                    }
                    else
                    {
                        // Without an intact base the patch cannot apply; drop
                        // the frame so the follow-up schedule renders it in
                        // full.
                        m_frameCacheStaleFrames.remove(frame);
                        m_frameCache.remove(frame);
                    }
                    clearCompletedFrameCacheRefresh();
                }
                else if (!image.isNull())
                {
                    m_cachedRenderSize = renderSize;
                    updateFrameCacheBudget();
                    const int cost =
                        PreviewRenderPolicy::cacheCostKiB(image.sizeInBytes());
                    m_frameCache.insert(frame, new QImage(image), cost);
                    m_frameCacheStaleFrames.remove(frame);
                }
                QTimer::singleShot(
                    0, this, &CanvasWidget::renderNextFrameCacheWarmup);
            });
        watcher->setFuture(QtConcurrent::run(&m_frameCacheWarmupPool,
            [document, cancellation, frame, renderSize]()
            {
                if (cancellation->load(std::memory_order_relaxed))
                {
                    return QImage();
                }
                QImage image =
                    RenderEngine::renderScaled(*document, frame, renderSize);
                if (cancellation->load(std::memory_order_relaxed))
                {
                    return QImage();
                }
                return image;
            }));
    }
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
    // A warmup used to stop playback outright until every frame was ready,
    // which read as a freeze after anything that invalidated frames: a wobble
    // setting, a committed stroke, resuming playback. Stepping only onto
    // frames the warmup has already produced keeps the animation moving as it
    // fills in, and never renders a frame on the GUI thread to do it.
    if (m_frameCacheWarmupActive)
    {
        const int frameCount =
            std::max(1, m_controller->document().animationFrames);
        const int nextFrame = (m_currentFrame + 1) % frameCount;
        if (!m_frameCache.object(nextFrame)
            || m_frameCacheStaleFrames.contains(nextFrame))
        {
            return;
        }
    }
    setCurrentFrame(m_currentFrame + 1);
}
}
