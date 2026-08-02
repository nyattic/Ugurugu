#include "ui/LayerThumbnailRenderer.hpp"

#include "document/LayerHierarchy.hpp"
#include "render/RenderEngine.hpp"

#include <algorithm>

namespace wobble
{

QSize LayerThumbnailRenderer::renderSize(const QSize &documentSize)
{
    if (!documentSize.isValid())
    {
        return {};
    }
    const QSize scaled = documentSize.scaled(targetSize, Qt::KeepAspectRatio);
    return QSize(std::max(1, scaled.width()), std::max(1, scaled.height()));
}

QImage LayerThumbnailRenderer::renderImage(
    const Document &document, const Layer &layer)
{
    const LayerHierarchyAnalysis hierarchy = analyzeLayerHierarchy(document);
    if (!hierarchy.isValid())
    {
        return {};
    }
    Document single = document;
    single.wobbleAmount = 0.0;
    single.layers.removeIf(
        [&hierarchy, &layer](const Layer &candidate)
        {
            return candidate.id != layer.id
                   && !hierarchy.isDescendantOf(candidate.id, layer.id);
        });
    if (Layer *root = single.layer(layer.id))
    {
        root->visible = true;
        root->opacity = 1.0;
        root->blendMode = LayerBlendMode::Normal;
        root->parentGroupId = {};
        root->clipToLayerBelow = false;
    }

    return RenderEngine::renderScaled(single, 0, renderSize(single.size));
}

QPixmap LayerThumbnailRenderer::render(
    const Document &document, const Layer &layer)
{
    const QImage image = renderImage(document, layer);
    if (image.isNull())
    {
        return {};
    }
    QPixmap pixmap = QPixmap::fromImage(image);
    pixmap.setDevicePixelRatio(2.0);
    return pixmap;
}

}
