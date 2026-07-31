#include "document/StrokeMask.hpp"

#include <QPainter>

#include <algorithm>

namespace wobble {

QImage transformedMask(
    const QImage &source,
    const QSize &targetSize,
    const QTransform &transform)
{
    if (source.isNull()) {
        return {};
    }
    QImage target(targetSize, QImage::Format_Grayscale8);
    if (target.isNull()) {
        return {};
    }
    target.fill(0);
    QPainter painter(&target);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter.setTransform(transform);
    painter.drawImage(QPointF(), source);
    painter.end();
    return target;
}

bool transformMask(
    QImage &mask,
    const QSize &targetSize,
    const QTransform &transform,
    QHash<qint64, QImage> &cache)
{
    if (mask.isNull()) {
        return true;
    }
    const qint64 key = mask.cacheKey();
    const auto cached = cache.constFind(key);
    if (cached != cache.cend()) {
        mask = cached.value();
        return true;
    }
    const QImage transformed =
        transformedMask(mask, targetSize, transform);
    if (transformed.isNull()) {
        return false;
    }
    cache.insert(key, transformed);
    mask = transformed;
    return true;
}

QImage maskedPart(
    const QImage &source,
    const QImage &selection,
    bool insideSelection)
{
    if (selection.isNull()
        || selection.format() != QImage::Format_Grayscale8
        || (!source.isNull()
            && (source.size() != selection.size()
                || source.format() != QImage::Format_Grayscale8))) {
        return {};
    }

    QImage result(selection.size(), QImage::Format_Grayscale8);
    if (result.isNull()) {
        return {};
    }
    for (int y = 0; y < selection.height(); ++y) {
        const uchar *sourceLine =
            source.isNull() ? nullptr : source.constScanLine(y);
        const uchar *selectionLine = selection.constScanLine(y);
        uchar *resultLine = result.scanLine(y);
        for (int x = 0; x < selection.width(); ++x) {
            const bool sourceContains =
                !sourceLine || sourceLine[x] >= 128;
            const bool selectionContains =
                selectionLine[x] >= 128;
            resultLine[x] =
                sourceContains
                    && (insideSelection
                            ? selectionContains
                            : !selectionContains)
                ? 255
                : 0;
        }
    }
    return result;
}

bool maskHasContent(const QImage &mask)
{
    if (mask.isNull()
        || mask.format() != QImage::Format_Grayscale8) {
        return false;
    }
    for (int y = 0; y < mask.height(); ++y) {
        const uchar *line = mask.constScanLine(y);
        if (std::any_of(
                line,
                line + mask.width(),
                [](uchar value) { return value >= 128; })) {
            return true;
        }
    }
    return false;
}

}
