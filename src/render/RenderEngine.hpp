#pragma once

#include "document/Document.hpp"

#include <QImage>
#include <QPainterPath>

namespace wobble {

class RenderEngine
{
public:
    struct LayerSplitFrame {
        QImage below;
        QImage layerBase;
        QImage above;
        qreal layerOpacity = 1.0;
        bool layerVisible = false;
        bool valid = false;
    };

    static QImage render(const Document &document, int frameIndex);
    static QImage renderScaled(
        const Document &document,
        int frameIndex,
        const QSize &outputSize);
    static LayerSplitFrame renderLayerSplit(
        const Document &document,
        int frameIndex,
        const QSize &outputSize,
        const QUuid &layerId);
    static bool renderStrokesOnLayer(
        QImage &layerImage,
        const Document &document,
        const QVector<Stroke> &strokes,
        int frameIndex,
        const QSize &outputSize);
    static QImage composeLayerSplit(
        const LayerSplitFrame &split,
        const QImage &layerImage);
    static QPainterPath strokePath(
        const Stroke &stroke,
        int frameIndex,
        int frameCount,
        qreal wobbleAmount);
    static QImage fillRegionMask(const QImage &image, const QPoint &seed);
};

}
