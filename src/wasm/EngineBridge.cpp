// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nyabi (nyattic)

#include "document/Document.hpp"
#include "io/DocumentSerializer.hpp"
#include "io/serializer/SerializerSchema.hpp"
#include "render/RenderEngine.hpp"

#include <QByteArray>
#include <QImage>
#include <QString>

#include <cstdint>
#include <emscripten/emscripten.h>
#include <utility>

namespace
{

struct BridgeDocument
{
    ugurugu::Document document;
    QImage renderedFrame;
    QByteArray serialized;
};

QByteArray &lastError()
{
    static QByteArray error;
    return error;
}

}

extern "C"
{

    EMSCRIPTEN_KEEPALIVE int ugu_schema_version()
    {
        return ugurugu::serializer_detail::schemaVersion;
    }

    EMSCRIPTEN_KEEPALIVE const char *ugu_last_error()
    {
        return lastError().constData();
    }

    EMSCRIPTEN_KEEPALIVE BridgeDocument *ugu_document_open(
        const std::uint8_t *data, int size)
    {
        QString error;
        const QByteArray bytes(reinterpret_cast<const char *>(data), size);
        auto document = ugurugu::DocumentSerializer::fromJson(bytes, &error);
        if (!document)
        {
            lastError() = error.toUtf8();
            return nullptr;
        }
        lastError().clear();
        return new BridgeDocument{std::move(*document), {}, {}};
    }

    EMSCRIPTEN_KEEPALIVE void ugu_document_close(BridgeDocument *handle)
    {
        delete handle;
    }

    EMSCRIPTEN_KEEPALIVE int ugu_document_width(const BridgeDocument *handle)
    {
        return handle->document.size.width();
    }

    EMSCRIPTEN_KEEPALIVE int ugu_document_height(const BridgeDocument *handle)
    {
        return handle->document.size.height();
    }

    EMSCRIPTEN_KEEPALIVE int ugu_document_frame_count(
        const BridgeDocument *handle)
    {
        return handle->document.animationFrames;
    }

    EMSCRIPTEN_KEEPALIVE int ugu_document_layer_count(
        const BridgeDocument *handle)
    {
        return static_cast<int>(handle->document.layers.size());
    }

    // The returned buffer is QImage::Format_ARGB32_Premultiplied: on the
    // little-endian wasm heap each pixel is the byte sequence B, G, R, A with
    // premultiplied color channels. Rows are ugu_frame_bytes_per_line() apart.
    // The pointer stays valid until the next render or close on the same
    // handle.
    EMSCRIPTEN_KEEPALIVE const std::uint8_t *ugu_render_frame(
        BridgeDocument *handle, int frameIndex)
    {
        handle->renderedFrame =
            ugurugu::RenderEngine::render(handle->document, frameIndex);
        if (handle->renderedFrame.isNull())
        {
            lastError() = QByteArrayLiteral("render produced a null frame");
            return nullptr;
        }
        lastError().clear();
        return handle->renderedFrame.constBits();
    }

    EMSCRIPTEN_KEEPALIVE int ugu_frame_width(const BridgeDocument *handle)
    {
        return handle->renderedFrame.width();
    }

    EMSCRIPTEN_KEEPALIVE int ugu_frame_height(const BridgeDocument *handle)
    {
        return handle->renderedFrame.height();
    }

    EMSCRIPTEN_KEEPALIVE int ugu_frame_bytes_per_line(
        const BridgeDocument *handle)
    {
        return static_cast<int>(handle->renderedFrame.bytesPerLine());
    }

    // The pointer stays valid until the next serialize or close on the same
    // handle.
    EMSCRIPTEN_KEEPALIVE const std::uint8_t *ugu_serialize(
        BridgeDocument *handle)
    {
        handle->serialized =
            ugurugu::DocumentSerializer::toJson(handle->document);
        if (handle->serialized.isEmpty())
        {
            lastError() = QByteArrayLiteral("serialization produced no bytes");
            return nullptr;
        }
        lastError().clear();
        return reinterpret_cast<const std::uint8_t *>(
            handle->serialized.constData());
    }

    EMSCRIPTEN_KEEPALIVE int ugu_serialized_size(const BridgeDocument *handle)
    {
        return static_cast<int>(handle->serialized.size());
    }
}
