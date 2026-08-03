#pragma once

#include "document/Document.hpp"

#include <QByteArray>
#include <QCoreApplication>
#include <QImage>
#include <QString>

#include <optional>

namespace wobble
{

// Clipboard payload for a masked layer selection. The payload is a complete
// single-layer document in the regular `.wagle` JSON schema, so pasting
// reuses the serializer's full validation, mask round-trip and schema
// version checks instead of introducing a second wire format.
class SelectionClipboardCodec
{
    Q_DECLARE_TR_FUNCTIONS(wobble::SelectionClipboardCodec)

public:
    struct Copy
    {
        QByteArray payload;
        // Selection-bounded ARGB32 frame for pasting into external apps.
        QImage raster;
        // The same layer the payload serializes, for callers that place the
        // copy into the current document without a clipboard round trip.
        Layer layer;
        QSize canvasSize;
    };

    struct Pasted
    {
        QSize canvasSize;
        Layer layer;
    };

    static QString mimeType();
    static std::optional<Copy> makeCopy(const Document &document,
        const QUuid &layerId,
        const QImage &selectionMask,
        int frameIndex,
        QString *error = nullptr);
    static std::optional<Pasted> decode(
        const QByteArray &payload, QString *error = nullptr);
};

}
