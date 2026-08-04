#pragma once

#include <QImage>

namespace ugurugu::FloodFillMask
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
