#pragma once

#include "document/Document.hpp"

#include <QHash>
#include <QImage>
#include <QSize>
#include <QTransform>

#include <optional>

namespace wobble
{

QImage transformedMask(
    const QImage &source, const QSize &targetSize, const QTransform &transform);

bool transformMask(QImage &mask,
    const QSize &targetSize,
    const QTransform &transform,
    QHash<qint64, QImage> &cache);

QImage maskedPart(
    const QImage &source, const QImage &selection, bool insideSelection);

bool maskHasContent(const QImage &mask);

bool maskHasContent(
    const QImage &mask, const std::optional<QRect> &visibilityClip);

bool masksIntersect(const QImage &first,
    const QImage &second,
    const std::optional<QRect> &visibilityClip = std::nullopt);

std::optional<QImage> materializedVisibilityMask(
    const Stroke &stroke, const QSize &canvasSize);

bool canonicalizeStrokeVisibility(Stroke &stroke, const QSize &canvasSize);

}
