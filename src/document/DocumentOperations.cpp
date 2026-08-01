#include "document/DocumentOperations.hpp"

#include "document/DocumentLimits.hpp"
#include "document/SelectionOperation.hpp"

#include <QSet>

#include <utility>

namespace wobble
{
namespace DocumentOperations
{

namespace
{

bool validCanvasSize(const QSize &size)
{
    return size.width() >= DocumentLimits::minimumCanvasEdge
           && size.height() >= DocumentLimits::minimumCanvasEdge
           && size.width() <= DocumentLimits::maximumCanvasEdge
           && size.height() <= DocumentLimits::maximumCanvasEdge;
}

}

QSize initialCanvasSize(
    const QVector<Stroke> &operations, const QSize &fallback)
{
    for (const Stroke &operation : operations)
    {
        if (operation.reframeOp)
        {
            return operation.reframeOp->sourceSize;
        }
        if (operation.pixelSelectionOp)
        {
            return operation.pixelSelectionOp->canvasSize;
        }
    }
    return fallback;
}

bool normalizeAndValidate(Document &document)
{
    if (!validCanvasSize(document.size))
    {
        return false;
    }

    Document candidate = document;
    QSet<qint64> maskKeys;
    quint64 distinctMaskBytes = 0;
    const auto registerMask = [&maskKeys, &distinctMaskBytes](
                                  const QImage &mask)
    {
        if (mask.isNull() || maskKeys.contains(mask.cacheKey()))
        {
            return true;
        }
        const quint64 bytes = mask.sizeInBytes();
        if (bytes
            > DocumentLimits::maximumDistinctClipMaskBytes - distinctMaskBytes)
        {
            return false;
        }
        maskKeys.insert(mask.cacheKey());
        distinctMaskBytes += bytes;
        return true;
    };

    for (Layer &layer : candidate.layers)
    {
        const QSize initialSize =
            layer.initialCanvasSize.isValid()
                ? layer.initialCanvasSize
                : initialCanvasSize(layer.strokes, candidate.size);
        if (!validCanvasSize(initialSize))
        {
            return false;
        }
        layer.initialCanvasSize = initialSize;
        QSize epochSize = initialSize;
        for (const Stroke &stroke : std::as_const(layer.strokes))
        {
            if (stroke.mode == StrokeMode::PixelSelection)
            {
                if (!stroke.pixelSelectionOp || stroke.reframeOp
                    || !stroke.points.isEmpty() || stroke.visibilityClip
                    || !stroke.clipMask.isNull() || !stroke.fillMask.isNull()
                    || stroke.pixelSelectionOp->canvasSize != epochSize
                    || !isValidPixelSelectionOp(*stroke.pixelSelectionOp))
                {
                    return false;
                }
                continue;
            }
            if (stroke.mode == StrokeMode::Reframe)
            {
                if (!stroke.reframeOp || stroke.pixelSelectionOp
                    || !stroke.points.isEmpty() || stroke.visibilityClip
                    || !stroke.clipMask.isNull() || !stroke.fillMask.isNull()
                    || stroke.reframeOp->sourceSize != epochSize
                    || !isValidReframeOp(*stroke.reframeOp))
                {
                    return false;
                }
                epochSize = stroke.reframeOp->targetSize;
                continue;
            }
            if ((stroke.mode != StrokeMode::Paint
                    && stroke.mode != StrokeMode::Erase
                    && stroke.mode != StrokeMode::Fill)
                || stroke.pixelSelectionOp || stroke.reframeOp
                || stroke.points.isEmpty()
                || (!stroke.clipMask.isNull()
                    && (stroke.clipMask.size() != epochSize
                        || stroke.clipMask.format()
                               != QImage::Format_Grayscale8))
                || (!stroke.fillMask.isNull()
                    && (stroke.mode != StrokeMode::Fill
                        || stroke.fillMask.size() != epochSize
                        || stroke.fillMask.format()
                               != QImage::Format_Grayscale8))
                || !registerMask(stroke.clipMask)
                || !registerMask(stroke.fillMask))
            {
                return false;
            }
        }
        if (epochSize != candidate.size)
        {
            return false;
        }
    }

    const quint64 packedBytes = packedSelectionBytes(candidate);
    if (packedBytes
        > DocumentLimits::maximumDistinctClipMaskBytes - distinctMaskBytes)
    {
        return false;
    }
    document = std::move(candidate);
    return true;
}

}

}
