#pragma once

#include <QImage>

namespace wobble::FloodFillMask
{

enum class Comparison
{
    AlphaBoundary,
    Color
};

QImage fromImage(const QImage &image,
    const QPoint &seed,
    Comparison comparison,
    int tolerance);

}
