#include "io/RenderExportPolicy.hpp"

#include "app/MemoryBudget.hpp"
#include "render/LayerCompositionPlan.hpp"

#include <algorithm>

namespace ugurugu
{

namespace
{

bool fitsMemoryBudget(const RenderExportMemoryEstimate &estimate)
{
    return estimate.valid && estimate.workingBytes > 0.0L
           && estimate.workingBytes <= static_cast<long double>(
                  MemoryBudget::animationExportWorkingBytes);
}

}

RenderExportMemoryEstimate RenderExportPolicy::staticImage(
    const Document &document)
{
    RenderExportMemoryEstimate estimate;
    const LayerCompositionMemoryEstimate hierarchy =
        LayerCompositionPlan::build(document).memoryEstimate(document.size);
    if (!hierarchy.valid)
    {
        return estimate;
    }
    estimate.valid = true;
    estimate.hierarchyTransientBytes =
        static_cast<long double>(hierarchy.peakBytes);
    estimate.workingBytes =
        estimate.hierarchyTransientBytes
        + static_cast<long double>(hierarchy.bytesPerSurface)
              * static_cast<long double>(
                  LayerCompositionPlan::paintOperationScratchSurfaceCount + 1);
    return estimate;
}

RenderExportMemoryEstimate RenderExportPolicy::animatedGif(
    const Document &document)
{
    return animatedGifAtSize(document, document.size);
}

RenderExportMemoryEstimate RenderExportPolicy::animatedGifAtSize(
    const Document &document, const QSize &outputSize)
{
    RenderExportMemoryEstimate estimate;
    if (outputSize.isEmpty())
    {
        return estimate;
    }
    const LayerCompositionPlan plan = LayerCompositionPlan::build(document);
    const LayerCompositionMemoryEstimate hierarchy =
        plan.memoryEstimate(outputSize);
    if (!hierarchy.valid || document.animationFrames <= 0)
    {
        return estimate;
    }
    const long double frameBytes =
        static_cast<long double>(hierarchy.bytesPerSurface);
    const long double frameCount =
        static_cast<long double>(document.animationFrames);
    const long double encoderBytes = frameBytes * frameCount * 3.0L;
    estimate.hierarchyTransientBytes =
        static_cast<long double>(hierarchy.peakBytes);
    long double renderBytes =
        estimate.hierarchyTransientBytes
        + frameBytes
              * (frameCount - 1.0L
                  + static_cast<long double>(
                      LayerCompositionPlan::paintOperationScratchSurfaceCount));
    if (outputSize != document.size)
    {
        // renderAtSize composes every paint layer at its native size before
        // scaling it down, so one native-size surface peaks alongside the
        // scaled hierarchy.
        const LayerCompositionMemoryEstimate nativeHierarchy =
            plan.memoryEstimate(document.size);
        if (!nativeHierarchy.valid)
        {
            return estimate;
        }
        renderBytes +=
            static_cast<long double>(nativeHierarchy.bytesPerSurface);
    }
    estimate.valid = true;
    estimate.workingBytes = std::max(encoderBytes, renderBytes);
    return estimate;
}

bool RenderExportPolicy::staticImageFitsMemoryBudget(const Document &document)
{
    return fitsMemoryBudget(staticImage(document));
}

bool RenderExportPolicy::animatedGifFitsMemoryBudget(const Document &document)
{
    return fitsMemoryBudget(animatedGif(document));
}

bool RenderExportPolicy::animatedGifFitsMemoryBudget(
    const Document &document, const QSize &outputSize)
{
    return fitsMemoryBudget(animatedGifAtSize(document, outputSize));
}

}
