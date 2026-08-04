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
    if (const Layer *strokeLayer = document.layer(m_activeStrokeLayer))
    {
        // The live stroke has to wobble the way its own layer does, not the
        // way the document does.
        const Document strokeDocument = documentForLayer(document, *strokeLayer);
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
                if (m_composedPreviewFrame.size() != baseFrame.size()
                    || m_composedPreviewBaseKey != baseFrame.cacheKey())
                {
                    m_composedPreviewFrame = baseFrame.copy();
                    m_composedPreviewBaseKey = baseFrame.cacheKey();
                }
                QPainter painter(&m_composedPreviewFrame);
                painter.setCompositionMode(QPainter::CompositionMode_Source);
                bool composedAllPatches = true;
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
                }
                painter.end();
                if (composedAllPatches)
                {
                    preview = m_composedPreviewFrame;
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
                    if (m_composedPreviewFrame.size() != baseFrame.size()
                        || m_composedPreviewBaseKey != baseFrame.cacheKey())
                    {
                        m_composedPreviewFrame = baseFrame.copy();
                        m_composedPreviewBaseKey = baseFrame.cacheKey();
                    }
                    QPainter painter(&m_composedPreviewFrame);
                    painter.setCompositionMode(
                        QPainter::CompositionMode_Source);
                    bool composedAllPatches = true;
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
                    }
                    painter.end();
                    if (composedAllPatches)
                    {
                        preview = m_composedPreviewFrame;
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
    }
    m_activeStrokePreview = preview;
    m_activeStrokePreviewRenderSize = renderSize;
    m_activeStrokePreviewFrame = m_currentFrame;
    m_activeStrokePreviewResolved = !preview.isNull();
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
        m_frameCache.clear();
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

void CanvasWidget::invalidateFrames()
{
    cancelFrameCacheWarmup();
    m_frameCache.clear();
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
    m_currentFrame %= frames;
    updateFrameCacheBudget();
    updateTimerInterval();
    notifyZoomChanged();
    update();
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
    m_frameCacheWarmupCursor = 0;
}

void CanvasWidget::scheduleFrameCacheWarmup()
{
    const quint64 generation = m_frameCacheWarmupGeneration;
    QTimer::singleShot(0,
        this,
        [this, generation]()
        {
            if (generation != m_frameCacheWarmupGeneration || !m_animating
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
                m_frameCache.clear();
                m_cachedRenderSize = renderSize;
            }

            QVector<int> missingFrames;
            missingFrames.reserve(frameCount);
            for (int offset = 0; offset < frameCount; ++offset)
            {
                const int frame = (m_currentFrame + offset) % frameCount;
                if (!m_frameCache.object(frame))
                {
                    missingFrames.append(frame);
                }
            }
            if (missingFrames.isEmpty())
            {
                return;
            }

            m_frameCacheWarmupDocument =
                std::make_shared<const Document>(displayDocument());
            m_frameCacheWarmupCancellation =
                std::make_shared<std::atomic_bool>(false);
            m_frameCacheWarmupFrames = std::move(missingFrames);
            m_frameCacheWarmupRenderSize = renderSize;
            m_frameCacheWarmupCursor = 0;
            m_frameCacheWarmupActive = true;
            renderNextFrameCacheWarmup();
        });
}

void CanvasWidget::renderNextFrameCacheWarmup()
{
    if (m_frameCacheWarmupWorkerRunning)
    {
        return;
    }
    if (!m_frameCacheWarmupActive || !m_frameCacheWarmupDocument
        || !m_frameCacheWarmupCancellation
        || m_frameCacheWarmupCursor >= m_frameCacheWarmupFrames.size())
    {
        m_frameCacheWarmupActive = false;
        m_frameCacheWarmupCancellation.reset();
        m_frameCacheWarmupDocument.reset();
        m_frameCacheWarmupFrames.clear();
        m_frameCacheWarmupRenderSize = {};
        m_frameCacheWarmupCursor = 0;
        update();
        return;
    }

    const quint64 generation = m_frameCacheWarmupGeneration;
    const int frame = m_frameCacheWarmupFrames[m_frameCacheWarmupCursor];
    const QSize renderSize = m_frameCacheWarmupRenderSize;
    const std::shared_ptr<const Document> document = m_frameCacheWarmupDocument;
    const std::shared_ptr<std::atomic_bool> cancellation =
        m_frameCacheWarmupCancellation;
    m_frameCacheWarmupWorkerRunning = true;
    connect(
        &m_frameCacheWarmupWatcher,
        &QFutureWatcher<QImage>::finished,
        this,
        [this, generation, frame, renderSize, cancellation]()
        {
            const QImage image = m_frameCacheWarmupWatcher.result();
            m_frameCacheWarmupWorkerRunning = false;
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
            if (!image.isNull())
            {
                m_cachedRenderSize = renderSize;
                updateFrameCacheBudget();
                const int cost =
                    PreviewRenderPolicy::cacheCostKiB(image.sizeInBytes());
                m_frameCache.insert(frame, new QImage(image), cost);
            }
            ++m_frameCacheWarmupCursor;
            QTimer::singleShot(
                0, this, &CanvasWidget::renderNextFrameCacheWarmup);
        },
        Qt::SingleShotConnection);
    m_frameCacheWarmupWatcher.setFuture(QtConcurrent::run(
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
        if (!m_frameCache.object((m_currentFrame + 1) % frameCount))
        {
            return;
        }
    }
    setCurrentFrame(m_currentFrame + 1);
}
}
