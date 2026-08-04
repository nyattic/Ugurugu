#pragma once

#include "app/MemoryBudget.hpp"

#include <QSize>
#include <QtTypes>

namespace ugurugu
{

class PreviewRenderPolicy final
{
public:
    // Follows installed memory, so it is a call rather than a constant.
    static int maximumCacheKiB();
    static constexpr qreal maximumPreviewEdge = 4096.0;

    // Surfaces an active stroke keeps resident alongside the frame it composes
    // over: the layer split's below, layerBase and above, the working copy
    // patches are drawn into, and bounded stable-prefix tile checkpoints. The
    // frame itself is counted by the caller as a retained surface.
    static constexpr int activeStrokeSurfaceCount = 5;

    static QSize renderSize(const QSize &documentSize,
        qreal physicalDisplayScale,
        int retainedSurfaceCount = 1,
        int hierarchyTransientSurfaceCount = 0);
    static int cacheCostKiB(qsizetype imageBytes);
    // Frame cache ceiling once the surfaces that cannot be dropped on demand
    // are accounted for. One frame always stays cacheable: an active stroke
    // re-copies its whole base whenever the frame it composes over is not the
    // same cached instance, which costs far more than the single-frame
    // overshoot that keeping it can cause.
    static int frameCacheCostKiB(qint64 pinnedBytes, qint64 singleFrameBytes);
};

}
