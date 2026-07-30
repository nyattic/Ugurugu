#include "document/Document.hpp"

namespace wobble {

Document Document::createDefault(const QSize &canvasSize)
{
    Document document;
    document.size = canvasSize;
    Layer layer;
    layer.name = QStringLiteral("Layer 1");
    document.activeLayerId = layer.id;
    document.layers.append(layer);
    return document;
}

Layer *Document::layer(const QUuid &id)
{
    const int index = layerIndex(id);
    return index >= 0 ? &layers[index] : nullptr;
}

const Layer *Document::layer(const QUuid &id) const
{
    const int index = layerIndex(id);
    return index >= 0 ? &layers[index] : nullptr;
}

int Document::layerIndex(const QUuid &id) const
{
    for (int index = 0; index < layers.size(); ++index) {
        if (layers[index].id == id) {
            return index;
        }
    }
    return -1;
}

}
