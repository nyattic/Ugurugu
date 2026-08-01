#pragma once

#include <QSize>

namespace wobble
{

class AnimationExportPolicy final
{
public:
    static long double estimatedWorkingBytes(
        const QSize &frameSize, qsizetype frameCount);
    static bool fitsMemoryBudget(const QSize &frameSize, qsizetype frameCount);
};

}
