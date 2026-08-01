#include "render/PreviewRenderPolicy.hpp"

#include <QtMath>

#include <algorithm>
#include <cmath>
#include <limits>

namespace wobble
{

QSize PreviewRenderPolicy::renderSize(
    const QSize &documentSize, qreal physicalDisplayScale)
{
    if (!documentSize.isValid() || !std::isfinite(physicalDisplayScale)
        || physicalDisplayScale <= 0.0)
    {
        return {};
    }
    const qreal edgeScale = std::min(maximumPreviewEdge / documentSize.width(),
        maximumPreviewEdge / documentSize.height());
    const qreal scale = std::clamp(std::min(physicalDisplayScale, edgeScale),
        1.0 / std::max(documentSize.width(), documentSize.height()),
        1.0);
    return QSize(std::max(1, qCeil(documentSize.width() * scale)),
        std::max(1, qCeil(documentSize.height() * scale)));
}

int PreviewRenderPolicy::cacheCostKiB(qsizetype imageBytes)
{
    return static_cast<int>(std::clamp<qsizetype>(
        imageBytes / 1024, 1, std::numeric_limits<int>::max()));
}

}
