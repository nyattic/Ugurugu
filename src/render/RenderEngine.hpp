#pragma once

#include "document/Document.hpp"

#include <QHash>
#include <QImage>
#include <QPainterPath>

namespace wobble
{

class RenderEngine
{
public:
    // DisplayPreview replays ordered framebuffer operations directly at the
    // requested display scale when the output is no larger on either axis
    // and smaller on at least one. NativeExact always renders the native
    // framebuffer first and only then scales it; use it for saved/exported
    // pixels and comparisons.
    enum class ScaledRenderMode
    {
        DisplayPreview,
        NativeExact
    };

    struct ScaledRenderStats
    {
        bool usedDisplayScaleReplay = false;
        bool usedNativeExactFallback = false;
        // DisplayPreview diagnostics for renderer-owned QImages only.
        // Existing document masks and private paint-engine scratch storage
        // are deliberately not counted. These remain zero on exact fallback.
        QSize largestIntermediateImageSize;
        quint64 largestIntermediateImageBytes = 0;
        quint64 maximumEstimatedWorkingSetBytes = 0;
        quint64 packedSelectionSamples = 0;
        quint64 primitiveStrokesRendered = 0;
        quint64 pixelSelectionOperationsReplayed = 0;
    };

    struct LayerSplitFrame
    {
        QImage below;
        QImage layerBase;
        QImage above;
        qreal layerOpacity = 1.0;
        LayerBlendMode layerBlendMode = LayerBlendMode::Normal;
        bool layerVisible = false;
        bool valid = false;
    };

    struct LayerRasterFrame
    {
        QHash<QUuid, QImage> paintLayers;
        QSize outputSize;
        bool valid = false;
    };

    struct StrokeCoveragePlan
    {
        struct Epoch
        {
            QSize canvasSize;
            int columns = 0;
            QHash<int, QVector<int>> effectIndexesByCell;
            QVector<int> globalEffectIndexes;
        };

        QVector<QSize> canvasBefore;
        QVector<QRect> primitiveBounds;
        QVector<int> epochBefore;
        QVector<Epoch> epochs;
        bool valid = false;
    };

    struct StrokeCoverageStats
    {
        quint64 fullCanvasFallbacks = 0;
        quint64 regionalRenders = 0;
        quint64 pixelSelectionOperationsReplayed = 0;
        quint64 reframeOperationsReplayed = 0;
        quint64 eraseOperationsReplayed = 0;
        quint64 effectCandidatesExamined = 0;
        quint64 maximumExplicitImageBytes = 0;
    };

    struct StrokeCoverageRegion
    {
        QImage image;
        QRect bounds;
        bool valid = false;
    };

    static QImage render(const Document &document, int frameIndex);
    static QImage renderScaled(const Document &document,
        int frameIndex,
        const QSize &outputSize,
        ScaledRenderMode mode = ScaledRenderMode::DisplayPreview,
        ScaledRenderStats *stats = nullptr);
    static LayerSplitFrame renderLayerSplit(const Document &document,
        int frameIndex,
        const QSize &outputSize,
        const QUuid &layerId,
        ScaledRenderMode mode = ScaledRenderMode::DisplayPreview,
        ScaledRenderStats *stats = nullptr);
    static LayerRasterFrame renderLayerRasterFrame(const Document &document,
        int frameIndex,
        const QSize &outputSize,
        qint64 maximumBytes,
        ScaledRenderMode mode = ScaledRenderMode::DisplayPreview,
        ScaledRenderStats *stats = nullptr);
    static QImage composeLayerRasterFrame(const Document &document,
        const LayerRasterFrame &frame,
        const QUuid &replacementLayerId,
        const QImage &replacementLayer);
    static bool renderStrokesOnLayer(QImage &layerImage,
        const Document &document,
        const QVector<Stroke> &strokes,
        int frameIndex,
        const QSize &outputSize);
    static bool renderStrokesOnLayerRegion(QImage &layerImage,
        const Document &document,
        const QVector<Stroke> &strokes,
        int frameIndex,
        const QRect &outputRegion);
    static QImage renderStrokeCoverage(const Document &document,
        const Layer &layer,
        int strokeIndex,
        int frameIndex);
    static StrokeCoveragePlan prepareStrokeCoverage(
        const Document &document, const Layer &layer);
    static QRect conservativeStrokeCoverageBounds(const Document &document,
        const Layer &layer,
        int strokeIndex,
        const StrokeCoveragePlan &plan);
    static StrokeCoverageRegion renderSparseStrokeCoverage(
        const Document &document,
        const Layer &layer,
        int strokeIndex,
        int frameIndex,
        const QRect &outputBounds,
        const StrokeCoveragePlan &plan,
        StrokeCoverageStats *stats = nullptr);
    static QImage renderStrokeCoverageRegion(const Document &document,
        const Layer &layer,
        int strokeIndex,
        int frameIndex,
        const QRect &outputBounds);
    static QImage composeLayerSplit(
        const LayerSplitFrame &split, const QImage &layerImage);
    // Replays one selection operation against an already rendered layer
    // framebuffer. DisplayPreview applies the scale-conjugated operation
    // directly when layerImage is no larger than operation.canvasSize on
    // either axis and smaller on at least one; otherwise the framebuffer must
    // be native-sized.
    static bool replayPixelSelectionOnLayer(QImage &layerImage,
        const PixelSelectionOp &operation,
        ScaledRenderMode mode = ScaledRenderMode::DisplayPreview,
        ScaledRenderStats *stats = nullptr);
    static QPainterPath strokePath(const Stroke &stroke,
        int frameIndex,
        int frameCount,
        qreal wobbleAmount);
    static QImage fillRegionMask(const QImage &image, const QPoint &seed);
};

}
