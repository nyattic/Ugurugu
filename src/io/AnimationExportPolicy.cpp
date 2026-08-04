#include "io/AnimationExportPolicy.hpp"

#include "app/MemoryBudget.hpp"
#include "io/RenderExportPolicy.hpp"

namespace ugurugu
{

long double AnimationExportPolicy::estimatedWorkingBytes(
    const QSize &frameSize, qsizetype frameCount)
{
    if (!frameSize.isValid() || frameCount <= 0)
    {
        return 0.0L;
    }
    return static_cast<long double>(frameSize.width())
           * static_cast<long double>(frameSize.height())
           * static_cast<long double>(frameCount) * 12.0L;
}

long double AnimationExportPolicy::estimatedWorkingBytes(
    const Document &document)
{
    return RenderExportPolicy::animatedGif(document).workingBytes;
}

bool AnimationExportPolicy::fitsMemoryBudget(
    const QSize &frameSize, qsizetype frameCount)
{
    const long double bytes = estimatedWorkingBytes(frameSize, frameCount);
    return bytes > 0.0L
           && bytes <= static_cast<long double>(
                  MemoryBudget::animationExportWorkingBytes);
}

bool AnimationExportPolicy::fitsMemoryBudget(const Document &document)
{
    return RenderExportPolicy::animatedGifFitsMemoryBudget(document);
}

}
