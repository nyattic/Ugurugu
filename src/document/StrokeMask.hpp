#pragma once

#include <QHash>
#include <QImage>
#include <QSize>
#include <QTransform>

namespace wobble {

QImage transformedMask(
    const QImage &source,
    const QSize &targetSize,
    const QTransform &transform);

bool transformMask(
    QImage &mask,
    const QSize &targetSize,
    const QTransform &transform,
    QHash<qint64, QImage> &cache);

QImage maskedPart(
    const QImage &source,
    const QImage &selection,
    bool insideSelection);

bool maskHasContent(const QImage &mask);

}
