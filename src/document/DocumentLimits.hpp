#pragma once

#include <QtGlobal>

namespace wobble
{

struct DocumentLimits final
{
    static constexpr int minimumCanvasEdge = 1;
    static constexpr int maximumCanvasEdge = 4096;
    static constexpr int minimumAnimationFrames = 2;
    static constexpr int maximumAnimationFrames = 60;
    static constexpr qreal minimumFramesPerSecond = 1.0;
    static constexpr qreal maximumFramesPerSecond = 50.0;
    static constexpr qreal minimumWobbleAmount = 0.0;
    static constexpr qreal maximumWobbleAmount = 12.0;
    static constexpr qreal minimumStrokeWidth = 0.25;
    static constexpr qreal maximumStrokeWidth = 512.0;
    static constexpr qreal minimumBrushSpacing = 0.02;
    static constexpr qreal maximumBrushSpacing = 2.0;
    static constexpr qreal maximumBrushScatter = 2.0;
    static constexpr qreal minimumBrushParticleSize = 0.01;
    static constexpr qreal maximumBrushParticleSize = 1.0;
    static constexpr qreal minimumBrushDensity = 0.05;
    static constexpr qreal maximumBrushDensity = 4.0;
    static constexpr int maximumLayerNameLength = 256;
    static constexpr int maximumLayers = 256;
    static constexpr int maximumStrokesPerLayer = 20000;
    static constexpr int maximumTotalStrokes = 20000;
    static constexpr int maximumPointsPerStroke = 200000;
    static constexpr qsizetype maximumTotalPoints = 250000;
    static constexpr qint64 maximumProjectBytes = 32LL * 1024LL * 1024LL;
    static constexpr quint64 maximumDistinctClipMaskBytes =
        256ULL * 1024ULL * 1024ULL;
    static constexpr quint64 maximumGifWorkingBytes =
        768ULL * 1024ULL * 1024ULL;
};

}
