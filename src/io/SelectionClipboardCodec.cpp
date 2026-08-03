#include "io/SelectionClipboardCodec.hpp"

#include "document/DocumentLimits.hpp"
#include "document/SelectionOperation.hpp"
#include "io/DocumentSerializer.hpp"
#include "render/RenderEngine.hpp"

namespace wobble
{

namespace
{

void setCodecError(QString *error, const QString &message)
{
    if (error)
    {
        *error = message;
    }
}

}

QString SelectionClipboardCodec::mimeType()
{
    return QStringLiteral("application/x-waglewaglepaint-selection+json");
}

std::optional<SelectionClipboardCodec::Copy> SelectionClipboardCodec::makeCopy(
    const Document &document,
    const QUuid &layerId,
    const QImage &selectionMask,
    int frameIndex,
    QString *error)
{
    const Layer *source = document.layer(layerId);
    if (!source || source->kind != LayerKind::Paint)
    {
        setCodecError(error, tr("The selected layer cannot be copied."));
        return std::nullopt;
    }
    if (selectionMask.isNull() || selectionMask.size() != document.size
        || selectionMask.format() != QImage::Format_Grayscale8)
    {
        setCodecError(error, tr("The selection could not be copied."));
        return std::nullopt;
    }
    const std::optional<PackedMaskRegion> packedSelection =
        packBinaryMask(selectionMask);
    if (!packedSelection)
    {
        setCodecError(error, tr("The selection is empty."));
        return std::nullopt;
    }

    Layer copy = *source;
    copy.parentGroupId = QUuid();
    copy.clipToLayerBelow = false;
    copy.visible = true;
    copy.reference = false;

    // Clipping to the selection is expressed as one appended erase operation
    // over the inverted mask instead of per-stroke clip masks. That stays
    // correct when the layer already carries ordered framebuffer operations,
    // and the copied strokes keep wobbling live inside the selection.
    QImage inverted = selectionMask;
    inverted.invertPixels();
    if (const std::optional<PixelSelectionOp> clipOperation =
            makePixelSelectionOp(inverted,
                QTransform(),
                /*clearSource=*/true,
                /*drawDestination=*/false))
    {
        if (copy.strokes.size() >= DocumentLimits::maximumStrokesPerLayer)
        {
            setCodecError(
                error, tr("The layer is too complex to copy."));
            return std::nullopt;
        }
        Stroke clip;
        clip.mode = StrokeMode::PixelSelection;
        clip.points.clear();
        clip.pixelSelectionOp = *clipOperation;
        copy.strokes.append(std::move(clip));
    }

    Document payloadDocument;
    payloadDocument.size = document.size;
    payloadDocument.background = QColor(0, 0, 0, 0);
    payloadDocument.animationFrames = document.animationFrames;
    payloadDocument.framesPerSecond = document.framesPerSecond;
    payloadDocument.wobbleAmount = document.wobbleAmount;
    payloadDocument.activeLayerId = copy.id;
    payloadDocument.layers = {copy};

    DocumentSerializer::SerializationCache cache;
    const std::optional<DocumentSerializer::PreparedDocument> prepared =
        DocumentSerializer::prepare(payloadDocument, cache, error);
    if (!prepared)
    {
        return std::nullopt;
    }
    Copy result;
    result.payload = DocumentSerializer::toJson(*prepared, cache);
    if (result.payload.isEmpty())
    {
        setCodecError(error, tr("The selection could not be copied."));
        return std::nullopt;
    }
    result.raster = RenderEngine::render(payloadDocument, frameIndex)
                        .copy(packedSelection->bounds)
                        .convertToFormat(QImage::Format_ARGB32);
    result.layer = std::move(copy);
    result.canvasSize = document.size;
    return result;
}

std::optional<SelectionClipboardCodec::Pasted> SelectionClipboardCodec::decode(
    const QByteArray &payload, QString *error)
{
    const std::optional<Document> document =
        DocumentSerializer::fromJson(payload, error);
    if (!document)
    {
        return std::nullopt;
    }
    if (document->layers.size() != 1
        || document->layers.first().kind != LayerKind::Paint)
    {
        setCodecError(error, tr("The clipboard content is not supported."));
        return std::nullopt;
    }
    Pasted result;
    result.canvasSize = document->size;
    result.layer = document->layers.first();
    return result;
}

}
