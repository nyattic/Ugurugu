#include "ui/LayerThumbnailRenderer.hpp"

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

QPixmap LayerThumbnailRenderer::render(
    const Document &document, const Layer &layer)
{
    Document single = document;
    single.wobbleAmount = 0.0;
    single.layers.removeIf(
        [&document, &layer](const Layer &candidate)
        {
            return candidate.id != layer.id
                   && !document.isLayerDescendantOf(candidate.id, layer.id);
        });
    if (Layer *root = single.layer(layer.id))
    {
        root->visible = true;
        root->opacity = 1.0;
        root->blendMode = LayerBlendMode::Normal;
        root->parentGroupId = {};
        root->clipToLayerBelow = false;
    }

    const QImage image =
        RenderEngine::renderScaled(single, 0, renderSize(single.size));
    if (image.isNull())
    {
        return {};
    }
    QPixmap pixmap = QPixmap::fromImage(image);
    pixmap.setDevicePixelRatio(2.0);
    return pixmap;
}

}
