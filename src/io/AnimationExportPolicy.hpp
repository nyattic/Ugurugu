#pragma once

#include "document/Document.hpp"

#include <QSize>

namespace wobble
{

class AnimationExportPolicy final
{
public:
    static long double estimatedWorkingBytes(
        const QSize &frameSize, qsizetype frameCount);
    static long double estimatedWorkingBytes(const Document &document);
    static bool fitsMemoryBudget(const QSize &frameSize, qsizetype frameCount);
    static bool fitsMemoryBudget(const Document &document);
};

}
