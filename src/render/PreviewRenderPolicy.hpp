#pragma once

#include "app/MemoryBudget.hpp"

#include <QSize>
#include <QtTypes>

namespace wobble
{

class PreviewRenderPolicy final
{
public:
    static constexpr int maximumCacheKiB = MemoryBudget::previewCacheKiB;
    static constexpr qreal maximumPreviewEdge = 4096.0;

    static QSize renderSize(const QSize &documentSize,
        qreal physicalDisplayScale,
        int retainedSurfaceCount = 1);
    static int cacheCostKiB(qsizetype imageBytes);
};

}
