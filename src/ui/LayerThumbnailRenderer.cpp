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
    Layer visibleLayer = layer;
    visibleLayer.visible = true;
    visibleLayer.opacity = 1.0;
    visibleLayer.blendMode = LayerBlendMode::Normal;
    single.layers = {visibleLayer};

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
