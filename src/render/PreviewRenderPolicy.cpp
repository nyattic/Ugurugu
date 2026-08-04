#include "render/PreviewRenderPolicy.hpp"

#include "render/LayerCompositionPlan.hpp"

#include <QtMath>

#include <algorithm>
#include <cmath>
#include <limits>

namespace ugurugu
{

QSize PreviewRenderPolicy::renderSize(const QSize &documentSize,
    qreal physicalDisplayScale,
    int retainedSurfaceCount,
    int hierarchyTransientSurfaceCount)
{
    if (documentSize.isEmpty() || !std::isfinite(physicalDisplayScale)
        || physicalDisplayScale <= 0.0)
    {
        return {};
    }
    const qreal edgeScale = std::min(maximumPreviewEdge / documentSize.width(),
        maximumPreviewEdge / documentSize.height());
    qreal scaleLimit = std::min(physicalDisplayScale, edgeScale);
    const qint64 surfaceCount =
        static_cast<qint64>(std::max(1, retainedSurfaceCount))
        + (hierarchyTransientSurfaceCount > 0
                ? static_cast<qint64>(hierarchyTransientSurfaceCount - 1)
                      + LayerCompositionPlan::paintOperationScratchSurfaceCount
                : 0);
    if (surfaceCount > 1)
    {
        const qreal retainedBytes = static_cast<qreal>(surfaceCount)
                                    * sizeof(quint32) * documentSize.width()
                                    * documentSize.height();
        const qreal budgetBytes = maximumCacheKiB * 1024.0 * 0.9;
        scaleLimit =
            std::min(scaleLimit, std::sqrt(budgetBytes / retainedBytes));
    }
    const qreal scale = std::clamp(scaleLimit,
        1.0 / std::max(documentSize.width(), documentSize.height()),
        1.0);
    return QSize(std::max(1, qCeil(documentSize.width() * scale)),
        std::max(1, qCeil(documentSize.height() * scale)));
}

int PreviewRenderPolicy::frameCacheCostKiB(
    qint64 pinnedBytes, qint64 singleFrameBytes)
{
    const qint64 budgetBytes = static_cast<qint64>(maximumCacheKiB) * 1024;
    const qint64 available = std::max(
        std::max<qint64>(singleFrameBytes, 0), budgetBytes - pinnedBytes);
    return cacheCostKiB(static_cast<qsizetype>(available));
}

int PreviewRenderPolicy::cacheCostKiB(qsizetype imageBytes)
{
    if (imageBytes <= 0)
    {
        return 1;
    }
    const qsizetype kibibytes =
        imageBytes / 1024 + (imageBytes % 1024 != 0 ? 1 : 0);
    return static_cast<int>(
        std::min<qsizetype>(kibibytes, std::numeric_limits<int>::max()));
}

}
