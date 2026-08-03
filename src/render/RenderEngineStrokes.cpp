#include "document/DocumentOperations.hpp"
#include "document/SelectionOperation.hpp"
#include "document/StrokeMask.hpp"
#include "render/RenderEngine.hpp"
#include "render/StrokeCoverageRenderer.hpp"
#include "render/StrokeRenderer.hpp"
#include "render/engine/LayerOperationReplay.hpp"
#include "render/engine/PreviewScale.hpp"

#include <QHash>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace wobble
{

using namespace render_detail;

bool RenderEngine::renderStrokesOnLayer(QImage &layerImage,
    const Document &document,
    const QVector<Stroke> &strokes,
    int frameIndex,
    const QSize &outputSize)
{
    if (document.size.isEmpty() || outputSize.isEmpty())
    {
        return false;
    }
    const bool containsFramebufferOperations = std::any_of(strokes.cbegin(),
        strokes.cend(),
        [](const Stroke &stroke)
        {
            return stroke.mode == StrokeMode::PixelSelection
                   || stroke.mode == StrokeMode::Reframe;
        });
    if (containsFramebufferOperations)
    {
        const int frameCount = std::max(1, document.animationFrames);
        const int normalizedFrame =
            ((frameIndex % frameCount) + frameCount) % frameCount;
        QImage native;
        if (!renderLayerOperations(native,
                document,
                strokes,
                normalizedFrame,
                frameCount,
                DocumentOperations::initialCanvasSize(strokes, document.size)))
        {
            return false;
        }
        layerImage = native.size() == outputSize ? native
                                                 : native.scaled(outputSize,
                                                       Qt::IgnoreAspectRatio,
                                                       Qt::FastTransformation);
        return !layerImage.isNull();
    }
    if (layerImage.isNull())
    {
        layerImage = QImage(outputSize, QImage::Format_ARGB32_Premultiplied);
        if (layerImage.isNull())
        {
            return false;
        }
        layerImage.fill(Qt::transparent);
    }
    if (layerImage.size() != outputSize
        || layerImage.format() != QImage::Format_ARGB32_Premultiplied)
    {
        return false;
    }

    const qreal horizontalScale =
        static_cast<qreal>(outputSize.width()) / document.size.width();
    const qreal verticalScale =
        static_cast<qreal>(outputSize.height()) / document.size.height();
    const int frameCount = std::max(1, document.animationFrames);
    const int normalizedFrame =
        ((frameIndex % frameCount) + frameCount) % frameCount;
    QHash<qint64, QPainterPath> clipPaths;
    QHash<qint64, QImage> scaledClipMasks;
    renderLayerStrokes(layerImage,
        document,
        strokes,
        normalizedFrame,
        frameCount,
        horizontalScale,
        verticalScale,
        clipPaths,
        scaledClipMasks);
    return true;
}

bool RenderEngine::renderStrokesOnLayerRegion(QImage &layerImage,
    const Document &document,
    const QVector<Stroke> &strokes,
    int frameIndex,
    const QRect &outputRegion)
{
    return renderStrokesOnLayerRegion(
        layerImage, document, strokes, frameIndex, document.size, outputRegion);
}

bool RenderEngine::renderStrokesOnLayerRegion(QImage &layerImage,
    const Document &document,
    const QVector<Stroke> &strokes,
    int frameIndex,
    const QSize &outputSize,
    const QRect &outputRegion,
    StrokeRenderCache *cache)
{
    if (document.size.isEmpty() || outputSize.isEmpty()
        || outputRegion.isEmpty()
        || !QRect(QPoint(), outputSize).contains(outputRegion)
        || layerImage.isNull() || layerImage.size() != outputRegion.size()
        || layerImage.format() != QImage::Format_ARGB32_Premultiplied
        || std::any_of(strokes.cbegin(),
            strokes.cend(),
            [](const Stroke &stroke)
            {
                return stroke.mode == StrokeMode::PixelSelection
                       || stroke.mode == StrokeMode::Reframe
                       || stroke.mode == StrokeMode::Fill;
            }))
    {
        return false;
    }
    const int frameCount = std::max(1, document.animationFrames);
    const int normalizedFrame =
        ((frameIndex % frameCount) + frameCount) % frameCount;
    QHash<qint64, QPainterPath> localClipPaths;
    QHash<qint64, QPainterPath> &clipPaths =
        cache ? cache->clipPaths : localClipPaths;
    QHash<qint64, QImage> scaledClipMasks;
    const qreal horizontalScale =
        static_cast<qreal>(outputSize.width()) / document.size.width();
    const qreal verticalScale =
        static_cast<qreal>(outputSize.height()) / document.size.height();
    const QPointF logicalOrigin(
        outputRegion.x() / horizontalScale, outputRegion.y() / verticalScale);
    renderLayerStrokes(layerImage,
        document,
        strokes,
        normalizedFrame,
        frameCount,
        horizontalScale,
        verticalScale,
        clipPaths,
        scaledClipMasks,
        logicalOrigin);
    return true;
}

QRect RenderEngine::strokePreviewBounds(
    const Document &document, const Stroke &stroke, const QSize &outputSize)
{
    if (document.size.isEmpty() || outputSize.isEmpty()
        || (stroke.mode != StrokeMode::Paint
            && stroke.mode != StrokeMode::Erase))
    {
        return {};
    }
    Layer layer;
    layer.initialCanvasSize = document.size;
    layer.strokes.append(stroke);
    const StrokeCoveragePlan plan = prepareStrokeCoverage(document, layer);
    const QRect nativeBounds =
        conservativeStrokeCoverageBounds(document, layer, 0, plan);
    if (nativeBounds.isEmpty())
    {
        return {};
    }
    const qreal horizontalScale =
        static_cast<qreal>(outputSize.width()) / document.size.width();
    const qreal verticalScale =
        static_cast<qreal>(outputSize.height()) / document.size.height();
    const QRectF mapped(nativeBounds.x() * horizontalScale,
        nativeBounds.y() * verticalScale,
        nativeBounds.width() * horizontalScale,
        nativeBounds.height() * verticalScale);
    return mapped.toAlignedRect()
        .adjusted(-1, -1, 1, 1)
        .intersected(QRect(QPoint(), outputSize));
}

QImage RenderEngine::renderStrokeCoverage(const Document &document,
    const Layer &layer,
    int strokeIndex,
    int frameIndex)
{
    if (!document.size.isValid() || layer.kind != LayerKind::Paint
        || strokeIndex < 0 || strokeIndex >= layer.strokes.size())
    {
        return {};
    }
    const Stroke &source = layer.strokes[strokeIndex];
    if (source.mode != StrokeMode::Paint && source.mode != StrokeMode::Erase
        && source.mode != StrokeMode::Fill)
    {
        return {};
    }

    QSize epochSize = layer.initialCanvasSize.isValid()
                          ? layer.initialCanvasSize
                          : DocumentOperations::initialCanvasSize(
                                layer.strokes, document.size);
    for (int index = 0; index < strokeIndex; ++index)
    {
        const Stroke &operation = layer.strokes[index];
        if (operation.mode == StrokeMode::Reframe)
        {
            if (!operation.reframeOp
                || operation.reframeOp->sourceSize != epochSize)
            {
                return {};
            }
            epochSize = operation.reframeOp->targetSize;
        }
    }
    if (!epochSize.isValid())
    {
        return {};
    }

    QImage coverage(epochSize, QImage::Format_ARGB32_Premultiplied);
    if (coverage.isNull())
    {
        return {};
    }
    coverage.fill(Qt::transparent);
    const int frameCount = std::max(1, document.animationFrames);
    const int normalizedFrame =
        ((frameIndex % frameCount) + frameCount) % frameCount;
    Stroke probe = source;
    if (probe.mode == StrokeMode::Erase)
    {
        probe.mode = StrokeMode::Paint;
        probe.color = Qt::white;
    }
    else
    {
        probe.color = QColor(255, 255, 255, source.color.alpha());
    }
    QHash<qint64, QPainterPath> clipPaths;
    QHash<qint64, QImage> scaledClipMasks;
    renderLayerStrokes(coverage,
        document,
        {probe},
        normalizedFrame,
        frameCount,
        1.0,
        1.0,
        clipPaths,
        scaledClipMasks);

    for (int index = strokeIndex + 1; index < layer.strokes.size(); ++index)
    {
        const Stroke &operation = layer.strokes[index];
        if (operation.mode == StrokeMode::PixelSelection)
        {
            if (!operation.pixelSelectionOp
                || !applyPixelSelectionOperation(
                    coverage, *operation.pixelSelectionOp))
            {
                return {};
            }
        }
        else if (operation.mode == StrokeMode::Reframe)
        {
            if (!operation.reframeOp
                || !applyReframeOperation(coverage, *operation.reframeOp))
            {
                return {};
            }
        }
        else if (operation.mode == StrokeMode::Erase)
        {
            renderLayerStrokes(coverage,
                document,
                {operation},
                normalizedFrame,
                frameCount,
                1.0,
                1.0,
                clipPaths,
                scaledClipMasks);
        }
    }
    return coverage.size() == document.size ? coverage : QImage();
}

RenderEngine::StrokeCoveragePlan RenderEngine::prepareStrokeCoverage(
    const Document &document, const Layer &layer)
{
    return StrokeCoverageRenderer::prepare(document, layer);
}

QRect RenderEngine::conservativeStrokeCoverageBounds(const Document &document,
    const Layer &layer,
    int strokeIndex,
    const StrokeCoveragePlan &plan)
{
    return StrokeCoverageRenderer::conservativeBounds(
        document, layer, strokeIndex, plan);
}

RenderEngine::StrokeCoverageRegion RenderEngine::renderSparseStrokeCoverage(
    const Document &document,
    const Layer &layer,
    int strokeIndex,
    int frameIndex,
    const QRect &outputBounds,
    const StrokeCoveragePlan &plan,
    StrokeCoverageStats *stats)
{
    return StrokeCoverageRenderer::render(
        document, layer, strokeIndex, frameIndex, outputBounds, plan, stats);
}

QImage RenderEngine::renderStrokeCoverageRegion(const Document &document,
    const Layer &layer,
    int strokeIndex,
    int frameIndex,
    const QRect &outputBounds)
{
    const QRect documentBounds(QPoint(), document.size);
    if (!document.size.isValid() || layer.kind != LayerKind::Paint
        || strokeIndex < 0 || strokeIndex >= layer.strokes.size()
        || outputBounds.isEmpty() || !documentBounds.contains(outputBounds))
    {
        return {};
    }
    const StrokeCoveragePlan plan = prepareStrokeCoverage(document, layer);
    const StrokeCoverageRegion sparse = renderSparseStrokeCoverage(
        document, layer, strokeIndex, frameIndex, outputBounds, plan);
    if (!sparse.valid)
    {
        return {};
    }
    QImage coverage(outputBounds.size(), QImage::Format_ARGB32_Premultiplied);
    if (coverage.isNull())
    {
        return {};
    }
    coverage.fill(Qt::transparent);
    if (!sparse.image.isNull())
    {
        QPainter painter(&coverage);
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.drawImage(
            sparse.bounds.topLeft() - outputBounds.topLeft(), sparse.image);
    }
    return coverage;
}

QPainterPath RenderEngine::strokePath(
    const Stroke &stroke, int frameIndex, int frameCount, qreal wobbleAmount)
{
    return StrokeRenderer::path(stroke, frameIndex, frameCount, wobbleAmount);
}
}
