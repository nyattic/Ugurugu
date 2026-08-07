// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nyabi (nyattic)

#include "document/Document.hpp"
#include "document/DocumentController.hpp"
#include "io/DocumentSerializer.hpp"
#include "io/serializer/SerializerSchema.hpp"
#include "render/RenderEngine.hpp"

#include <QByteArray>
#include <QColor>
#include <QImage>
#include <QRandomGenerator>
#include <QString>
#include <QUuid>

#include <cstdint>
#include <emscripten/emscripten.h>
#include <memory>
#include <utility>

namespace
{

struct BridgeDocument
{
    std::unique_ptr<ugurugu::DocumentController> controller =
        std::make_unique<ugurugu::DocumentController>();
    ugurugu::Stroke brushTemplate;
    ugurugu::Stroke activeStroke;
    bool strokeInProgress = false;
    QImage renderedFrame;
    QByteArray serialized;
};

QByteArray &lastError()
{
    static QByteArray error;
    return error;
}

QUuid paintTargetLayer(const ugurugu::Document &document)
{
    if (const ugurugu::Layer *layer = document.layer(document.activeLayerId);
        layer != nullptr && layer->kind == ugurugu::LayerKind::Paint)
    {
        return document.activeLayerId;
    }
    for (const ugurugu::Layer &layer : document.layers)
    {
        if (layer.kind == ugurugu::LayerKind::Paint)
        {
            return layer.id;
        }
    }
    return {};
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
        auto handle = std::make_unique<BridgeDocument>();
        if (!handle->controller->loadDocument(std::move(*document), &error))
        {
            lastError() = error.toUtf8();
            return nullptr;
        }
        lastError().clear();
        return handle.release();
    }

    EMSCRIPTEN_KEEPALIVE void ugu_document_close(BridgeDocument *handle)
    {
        delete handle;
    }

    EMSCRIPTEN_KEEPALIVE int ugu_document_width(const BridgeDocument *handle)
    {
        return handle->controller->document().size.width();
    }

    EMSCRIPTEN_KEEPALIVE int ugu_document_height(const BridgeDocument *handle)
    {
        return handle->controller->document().size.height();
    }

    EMSCRIPTEN_KEEPALIVE int ugu_document_frame_count(
        const BridgeDocument *handle)
    {
        return handle->controller->document().animationFrames;
    }

    EMSCRIPTEN_KEEPALIVE int ugu_document_layer_count(
        const BridgeDocument *handle)
    {
        return static_cast<int>(handle->controller->document().layers.size());
    }

    EMSCRIPTEN_KEEPALIVE double ugu_document_fps(const BridgeDocument *handle)
    {
        return handle->controller->document().framesPerSecond;
    }

    EMSCRIPTEN_KEEPALIVE void ugu_set_brush(BridgeDocument *handle,
        int red,
        int green,
        int blue,
        int alpha,
        double width,
        int erase)
    {
        handle->brushTemplate.color = QColor(red, green, blue, alpha);
        handle->brushTemplate.width = width;
        handle->brushTemplate.mode = erase != 0 ? ugurugu::StrokeMode::Erase
                                                : ugurugu::StrokeMode::Paint;
    }

    EMSCRIPTEN_KEEPALIVE int ugu_stroke_begin(
        BridgeDocument *handle, double x, double y, double pressure)
    {
        const QUuid layerId = paintTargetLayer(handle->controller->document());
        if (layerId.isNull())
        {
            lastError() = QByteArrayLiteral("document has no paint layer");
            return 0;
        }
        handle->activeStroke = handle->brushTemplate;
        handle->activeStroke.id = QUuid::createUuid();
        handle->activeStroke.seed = QRandomGenerator::global()->generate64();
        handle->activeStroke.points = {
            ugurugu::StrokePoint{QPointF(x, y), pressure}};
        handle->strokeInProgress = true;
        lastError().clear();
        return 1;
    }

    EMSCRIPTEN_KEEPALIVE void ugu_stroke_append(
        BridgeDocument *handle, double x, double y, double pressure)
    {
        if (!handle->strokeInProgress)
        {
            return;
        }
        handle->activeStroke.points.append(
            ugurugu::StrokePoint{QPointF(x, y), pressure});
    }

    EMSCRIPTEN_KEEPALIVE int ugu_stroke_end(BridgeDocument *handle)
    {
        if (!handle->strokeInProgress)
        {
            return -1;
        }
        handle->strokeInProgress = false;
        const QUuid layerId = paintTargetLayer(handle->controller->document());
        const auto result = handle->controller->addStroke(
            layerId, std::move(handle->activeStroke));
        handle->activeStroke = {};
        using AddStrokeResult = ugurugu::DocumentController::AddStrokeResult;
        if (result != AddStrokeResult::Added
            && result != AddStrokeResult::AddedWithResampledPoints)
        {
            lastError() = QByteArrayLiteral("stroke rejected");
        }
        return static_cast<int>(result);
    }

    EMSCRIPTEN_KEEPALIVE void ugu_stroke_cancel(BridgeDocument *handle)
    {
        handle->strokeInProgress = false;
        handle->activeStroke = {};
    }

    EMSCRIPTEN_KEEPALIVE int ugu_can_undo(const BridgeDocument *handle)
    {
        return handle->controller->undoStack()->canUndo() ? 1 : 0;
    }

    EMSCRIPTEN_KEEPALIVE int ugu_can_redo(const BridgeDocument *handle)
    {
        return handle->controller->undoStack()->canRedo() ? 1 : 0;
    }

    EMSCRIPTEN_KEEPALIVE void ugu_undo(BridgeDocument *handle)
    {
        handle->controller->undoStack()->undo();
    }

    EMSCRIPTEN_KEEPALIVE void ugu_redo(BridgeDocument *handle)
    {
        handle->controller->undoStack()->redo();
    }

    // The returned buffer is QImage::Format_ARGB32_Premultiplied: on the
    // little-endian wasm heap each pixel is the byte sequence B, G, R, A with
    // premultiplied color channels. Rows are ugu_frame_bytes_per_line() apart.
    // The pointer stays valid until the next render or close on the same
    // handle. A stroke in progress is composed on top of the committed
    // document so pointer sampling can preview without touching history.
    EMSCRIPTEN_KEEPALIVE const std::uint8_t *ugu_render_frame(
        BridgeDocument *handle, int frameIndex)
    {
        const ugurugu::Document &committed = handle->controller->document();
        if (handle->strokeInProgress && !handle->activeStroke.points.isEmpty())
        {
            ugurugu::Document preview = committed;
            if (ugurugu::Layer *layer =
                    preview.layer(paintTargetLayer(preview)))
            {
                layer->strokes.append(handle->activeStroke);
            }
            handle->renderedFrame =
                ugurugu::RenderEngine::render(preview, frameIndex);
        }
        else
        {
            handle->renderedFrame =
                ugurugu::RenderEngine::render(committed, frameIndex);
        }
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
            ugurugu::DocumentSerializer::toJson(handle->controller->document());
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
