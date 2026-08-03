#pragma once

#include <QtTypes>

namespace wobble
{

// Byte totals for the preview surfaces a canvas can retain at the same time.
//
// The declared preview budget covers all of these together. Bounding a single
// category against the whole budget is what lets the real peak exceed it: the
// frame cache and the layer raster cache were each capped at the full budget
// and can be resident at once, while the split, composed and colour-pick
// surfaces were never counted at all.
struct PreviewSurfaceUsage final
{
    qint64 frameCacheBytes = 0;
    qint64 layerSplitBytes = 0;
    qint64 layerRasterBytes = 0;
    qint64 composedPreviewBytes = 0;
    qint64 colorPickBytes = 0;
    qint64 strokeTileBytes = 0;

    qint64 totalBytes() const
    {
        return frameCacheBytes + layerSplitBytes + layerRasterBytes
               + composedPreviewBytes + colorPickBytes + strokeTileBytes;
    }

    // Everything the canvas cannot drop on demand. The frame cache is the only
    // category that is safe to evict mid-interaction, so it is what has to
    // absorb the rest of the budget.
    qint64 pinnedBytes() const
    {
        return totalBytes() - frameCacheBytes;
    }
};

}
