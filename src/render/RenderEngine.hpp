#pragma once

#include "document/Document.hpp"

#include <QImage>
#include <QPainterPath>

namespace wobble {

class RenderEngine
{
public:
    static QImage render(const Document &document, int frameIndex);
    static QPainterPath strokePath(
        const Stroke &stroke,
        int frameIndex,
        int frameCount,
        qreal wobbleAmount);
};

}
