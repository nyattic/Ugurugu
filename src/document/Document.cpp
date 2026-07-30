#include "document/Document.hpp"

#include "document/DocumentLimits.hpp"

#include <cmath>

namespace wobble {

bool isValidBrushSettings(const BrushSettings &settings)
{
    const bool validEngine =
        settings.engine == BrushEngine::Line
        || settings.engine == BrushEngine::Airbrush
        || settings.engine == BrushEngine::Spray;
    const bool validTip =
        settings.tipShape == BrushTipShape::Round
        || settings.tipShape == BrushTipShape::Square;
    return validEngine
        && validTip
        && std::isfinite(settings.opacity)
        && settings.opacity >= 0.0
        && settings.opacity <= 1.0
        && std::isfinite(settings.flow)
        && settings.flow > 0.0
        && settings.flow <= 1.0
        && std::isfinite(settings.hardness)
        && settings.hardness >= 0.0
        && settings.hardness <= 1.0
        && std::isfinite(settings.spacing)
        && settings.spacing >= DocumentLimits::minimumBrushSpacing
        && settings.spacing <= DocumentLimits::maximumBrushSpacing
        && std::isfinite(settings.scatter)
        && settings.scatter >= 0.0
        && settings.scatter <= DocumentLimits::maximumBrushScatter
        && std::isfinite(settings.particleSize)
        && settings.particleSize >= DocumentLimits::minimumBrushParticleSize
        && settings.particleSize <= DocumentLimits::maximumBrushParticleSize
        && std::isfinite(settings.density)
        && settings.density >= DocumentLimits::minimumBrushDensity
        && settings.density <= DocumentLimits::maximumBrushDensity
        && std::isfinite(settings.sizeDynamics)
        && settings.sizeDynamics >= 0.0
        && settings.sizeDynamics <= 1.0
        && std::isfinite(settings.opacityDynamics)
        && settings.opacityDynamics >= 0.0
        && settings.opacityDynamics <= 1.0
        && std::isfinite(settings.sizeJitter)
        && settings.sizeJitter >= 0.0
        && settings.sizeJitter <= 1.0;
}

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
