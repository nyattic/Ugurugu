#pragma once

#include "document/Document.hpp"

#include <QImage>
#include <QPainterPath>

namespace wobble {

class RenderEngine
{
public:
    static QImage render(const Document &document, int frameIndex);
    static QImage renderScaled(
        const Document &document,
        int frameIndex,
        const QSize &outputSize);
    static QPainterPath strokePath(
        const Stroke &stroke,
        int frameIndex,
        int frameCount,
        qreal wobbleAmount);
    static QImage fillRegionMask(const QImage &image, const QPoint &seed);
};

}
