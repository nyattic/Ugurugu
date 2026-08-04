#pragma once

#include "document/Document.hpp"

#include <QPointF>
#include <QVector>

#include <optional>

namespace ugurugu::FrozenFillMask
{

// Odd-even fill and binary 0/255 coverage are persisted rendering invariants.
std::optional<QImage> fromPolygon(
    const QSize &canvasSize, const QVector<QPointF> &polygon);
std::optional<PackedMaskRegion> packedFromPolygon(
    const QSize &canvasSize, const QVector<QPointF> &polygon);

}
