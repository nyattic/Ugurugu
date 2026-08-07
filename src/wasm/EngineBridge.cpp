// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nyabi (nyattic)

#include "brush/BrushPreset.hpp"
#include "document/Document.hpp"
#include "document/DocumentController.hpp"
#include "input/StrokeStabilizer.hpp"
#include "io/AnimationExportPolicy.hpp"
#include "io/DocumentSerializer.hpp"
#include "io/GifWriter.hpp"
#include "io/serializer/SerializerSchema.hpp"
#include "render/IncrementalStrokeRenderer.hpp"
#include "render/LayerThumbnailRenderer.hpp"
#include "render/RenderEngine.hpp"

#include <QByteArray>
#include <QColor>
#include <QFile>
#include <QImage>
#include <QPainter>
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
    ugurugu::StrokeStabilizer stabilizer;
    ugurugu::Stroke activeStroke;
    bool strokeInProgress = false;
    int strokeFrame = 0;
    QPointF lastRawPosition;
    quint64 lastTimestamp = 0;
    ugurugu::RenderEngine::LayerSplitFrame split;
    ugurugu::IncrementalStrokeRenderer incremental;
    QUuid strokeLayerId;
    bool incrementalActive = false;
    QImage renderedFrame;
    QRect dirty;
    QImage thumbnail;
    QByteArray serialized;
    QByteArray exportBytes;
    QByteArray scratchText;
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

const ugurugu::Layer *layerAtIndex(const BridgeDocument *handle, int index)
{
    const auto &layers = handle->controller->document().layers;
    if (index < 0 || index >= layers.size())
    {
        return nullptr;
    }
    return &layers[index];
}

void renderCommittedFrame(BridgeDocument *handle, int frameIndex)
{
    handle->renderedFrame = ugurugu::RenderEngine::render(
        handle->controller->document(), frameIndex);
    handle->dirty = handle->renderedFrame.rect();
}

void renderFullStrokePreview(BridgeDocument *handle)
{
    ugurugu::Document preview = handle->controller->document();
    if (ugurugu::Layer *layer = preview.layer(paintTargetLayer(preview)))
    {
        layer->strokes.append(handle->activeStroke);
    }
    handle->renderedFrame =
        ugurugu::RenderEngine::render(preview, handle->strokeFrame);
    handle->dirty = handle->renderedFrame.rect();
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

    EMSCRIPTEN_KEEPALIVE int ugu_brush_preset_count()
    {
        return static_cast<int>(ugurugu::BrushPresetCatalog::builtIns().size());
    }

    EMSCRIPTEN_KEEPALIVE const char *ugu_brush_preset_name(
        BridgeDocument *handle, int index)
    {
        const auto &presets = ugurugu::BrushPresetCatalog::builtIns();
        if (index < 0 || index >= presets.size())
        {
            return "";
        }
        handle->scratchText =
            ugurugu::BrushPresetCatalog::displayName(presets[index]).toUtf8();
        return handle->scratchText.constData();
    }

    EMSCRIPTEN_KEEPALIVE double ugu_brush_preset_default_size(int index)
    {
        const auto &presets = ugurugu::BrushPresetCatalog::builtIns();
        if (index < 0 || index >= presets.size())
        {
            return 6.0;
        }
        return presets[index].defaultSize;
    }

    EMSCRIPTEN_KEEPALIVE void ugu_set_brush_preset(
        BridgeDocument *handle, int index)
    {
        const auto &presets = ugurugu::BrushPresetCatalog::builtIns();
        if (index < 0 || index >= presets.size())
        {
            return;
        }
        handle->brushTemplate.brush = presets[index].settings;
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

    EMSCRIPTEN_KEEPALIVE void ugu_set_stabilization(
        BridgeDocument *handle, double strength)
    {
        handle->stabilizer.setStrength(strength);
    }

    EMSCRIPTEN_KEEPALIVE int ugu_stroke_begin(BridgeDocument *handle,
        int frame,
        double x,
        double y,
        double pressure,
        double timestamp)
    {
        const ugurugu::Document &document = handle->controller->document();
        const QUuid layerId = paintTargetLayer(document);
        if (layerId.isNull())
        {
            lastError() = QByteArrayLiteral("document has no paint layer");
            return 0;
        }
        handle->strokeFrame = frame;
        handle->strokeLayerId = layerId;
        handle->activeStroke = handle->brushTemplate;
        handle->activeStroke.id = QUuid::createUuid();
        handle->activeStroke.seed = QRandomGenerator::global()->generate64();
        const QPointF raw(x, y);
        const auto time = static_cast<quint64>(timestamp);
        handle->activeStroke.points = {ugurugu::StrokePoint{
            handle->stabilizer.begin(raw, time), pressure}};
        handle->lastRawPosition = raw;
        handle->lastTimestamp = time;
        handle->strokeInProgress = true;

        handle->split = ugurugu::RenderEngine::renderLayerSplit(document,
            frame,
            document.size,
            layerId,
            ugurugu::RenderEngine::ScaledRenderMode::NativeExact);
        handle->incrementalActive = handle->split.valid;
        if (handle->incrementalActive)
        {
            handle->incremental.clear();
        }
        renderCommittedFrame(handle, frame);
        lastError().clear();
        return 1;
    }

    EMSCRIPTEN_KEEPALIVE void ugu_stroke_append(BridgeDocument *handle,
        double x,
        double y,
        double pressure,
        double timestamp)
    {
        if (!handle->strokeInProgress)
        {
            return;
        }
        const QPointF raw(x, y);
        const auto time = static_cast<quint64>(timestamp);
        handle->activeStroke.points.append(ugurugu::StrokePoint{
            handle->stabilizer.update(raw, time), pressure});
        handle->lastRawPosition = raw;
        handle->lastTimestamp = time;
    }

    // Renders the active stroke's pending points into the frame buffer and
    // narrows the dirty rectangle to the touched tiles when the incremental
    // path holds; otherwise the whole frame is re-rendered and dirty.
    EMSCRIPTEN_KEEPALIVE int ugu_stroke_render(BridgeDocument *handle)
    {
        if (!handle->strokeInProgress)
        {
            return 0;
        }
        if (!handle->incrementalActive)
        {
            renderFullStrokePreview(handle);
            return 1;
        }
        const ugurugu::Document &document = handle->controller->document();
        const ugurugu::Layer *strokeLayer =
            document.layer(handle->strokeLayerId);
        if (strokeLayer == nullptr)
        {
            handle->incrementalActive = false;
            renderFullStrokePreview(handle);
            return 1;
        }
        // The live stroke has to wobble the way its own layer does, not the
        // way the document does.
        const ugurugu::Document strokeDocument =
            ugurugu::documentForLayer(document, *strokeLayer);
        const auto update = handle->incremental.update(handle->split.layerBase,
            strokeDocument,
            handle->activeStroke,
            handle->strokeFrame,
            document.size);
        if (!update.valid)
        {
            handle->incrementalActive = false;
            renderFullStrokePreview(handle);
            return 1;
        }
        QRect dirty;
        QPainter painter(&handle->renderedFrame);
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        for (const auto &patch : update.patches)
        {
            // composeLayerSplitRegion wants the tile-sized patch image, not a
            // full layer surface; a null result means the contract broke, so
            // fall back to the full preview instead of dropping the patch.
            const QImage region =
                ugurugu::RenderEngine::composeLayerSplitRegion(
                    handle->split, patch.layerImage, patch.bounds);
            if (region.isNull())
            {
                painter.end();
                handle->incrementalActive = false;
                renderFullStrokePreview(handle);
                return 1;
            }
            painter.drawImage(patch.bounds.topLeft(), region);
            dirty = dirty.united(patch.bounds);
        }
        painter.end();
        handle->dirty = dirty.intersected(handle->renderedFrame.rect());
        return 1;
    }

    EMSCRIPTEN_KEEPALIVE int ugu_stroke_end(BridgeDocument *handle)
    {
        if (!handle->strokeInProgress)
        {
            return -1;
        }
        const QPointF finished = handle->stabilizer.finish(
            handle->lastRawPosition, handle->lastTimestamp);
        if (!handle->activeStroke.points.isEmpty()
            && handle->activeStroke.points.last().position != finished)
        {
            handle->activeStroke.points.append(ugurugu::StrokePoint{
                finished, handle->activeStroke.points.last().pressure});
        }
        handle->strokeInProgress = false;
        handle->incrementalActive = false;
        handle->split = {};
        const QUuid layerId = paintTargetLayer(handle->controller->document());
        const auto result = handle->controller->addStroke(
            layerId, std::move(handle->activeStroke));
        handle->activeStroke = {};
        renderCommittedFrame(handle, handle->strokeFrame);
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
        handle->incrementalActive = false;
        handle->split = {};
        handle->activeStroke = {};
        renderCommittedFrame(handle, handle->strokeFrame);
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

    EMSCRIPTEN_KEEPALIVE const char *ugu_layer_name(
        BridgeDocument *handle, int index)
    {
        const ugurugu::Layer *layer = layerAtIndex(handle, index);
        if (layer == nullptr)
        {
            return "";
        }
        handle->scratchText = layer->name.toUtf8();
        return handle->scratchText.constData();
    }

    EMSCRIPTEN_KEEPALIVE int ugu_layer_kind(
        const BridgeDocument *handle, int index)
    {
        const ugurugu::Layer *layer = layerAtIndex(handle, index);
        return layer != nullptr && layer->kind == ugurugu::LayerKind::Group ? 1
                                                                            : 0;
    }

    EMSCRIPTEN_KEEPALIVE int ugu_layer_visible(
        const BridgeDocument *handle, int index)
    {
        const ugurugu::Layer *layer = layerAtIndex(handle, index);
        return layer != nullptr && layer->visible ? 1 : 0;
    }

    EMSCRIPTEN_KEEPALIVE double ugu_layer_opacity(
        const BridgeDocument *handle, int index)
    {
        const ugurugu::Layer *layer = layerAtIndex(handle, index);
        return layer != nullptr ? layer->opacity : 1.0;
    }

    EMSCRIPTEN_KEEPALIVE int ugu_layer_is_active(
        const BridgeDocument *handle, int index)
    {
        const ugurugu::Layer *layer = layerAtIndex(handle, index);
        return layer != nullptr
                       && layer->id
                              == handle->controller->document().activeLayerId
                   ? 1
                   : 0;
    }

    EMSCRIPTEN_KEEPALIVE int ugu_layer_depth(
        const BridgeDocument *handle, int index)
    {
        const ugurugu::Layer *layer = layerAtIndex(handle, index);
        if (layer == nullptr)
        {
            return 0;
        }
        return handle->controller->document().layerDepth(layer->id);
    }

    EMSCRIPTEN_KEEPALIVE void ugu_layer_activate(
        BridgeDocument *handle, int index)
    {
        if (const ugurugu::Layer *layer = layerAtIndex(handle, index))
        {
            handle->controller->setActiveLayer(layer->id);
        }
    }

    EMSCRIPTEN_KEEPALIVE void ugu_layer_set_visible(
        BridgeDocument *handle, int index, int visible)
    {
        if (const ugurugu::Layer *layer = layerAtIndex(handle, index))
        {
            handle->controller->setLayerVisible(layer->id, visible != 0);
        }
    }

    EMSCRIPTEN_KEEPALIVE void ugu_layer_set_opacity(
        BridgeDocument *handle, int index, double opacity)
    {
        if (const ugurugu::Layer *layer = layerAtIndex(handle, index))
        {
            handle->controller->setLayerOpacity(layer->id, opacity);
        }
    }

    EMSCRIPTEN_KEEPALIVE void ugu_layer_add(BridgeDocument *handle)
    {
        const ugurugu::Document &document = handle->controller->document();
        QUuid parentGroupId;
        if (const ugurugu::Layer *active =
                document.layer(document.activeLayerId))
        {
            parentGroupId = active->parentGroupId;
        }
        handle->controller->addLayer(parentGroupId);
    }

    EMSCRIPTEN_KEEPALIVE void ugu_layer_remove(
        BridgeDocument *handle, int index)
    {
        if (const ugurugu::Layer *layer = layerAtIndex(handle, index))
        {
            handle->controller->removeLayer(layer->id);
        }
    }

    EMSCRIPTEN_KEEPALIVE void ugu_layer_rename(
        BridgeDocument *handle, int index, const char *name)
    {
        if (const ugurugu::Layer *layer = layerAtIndex(handle, index))
        {
            handle->controller->renameLayer(layer->id, QString::fromUtf8(name));
        }
    }

    EMSCRIPTEN_KEEPALIVE void ugu_layer_move(
        BridgeDocument *handle, int index, int offset)
    {
        if (const ugurugu::Layer *layer = layerAtIndex(handle, index))
        {
            handle->controller->moveLayer(layer->id, offset);
        }
    }

    // The returned buffer is QImage::Format_ARGB32_Premultiplied: on the
    // little-endian wasm heap each pixel is the byte sequence B, G, R, A with
    // premultiplied color channels. Rows are ugu_frame_bytes_per_line() apart.
    // The pointer stays valid until the next render or close on the same
    // handle. ugu_dirty_* describe the region the last render actually
    // changed; consumers may upload just that region.
    EMSCRIPTEN_KEEPALIVE const std::uint8_t *ugu_render_frame(
        BridgeDocument *handle, int frameIndex)
    {
        renderCommittedFrame(handle, frameIndex);
        if (handle->renderedFrame.isNull())
        {
            lastError() = QByteArrayLiteral("render produced a null frame");
            return nullptr;
        }
        lastError().clear();
        return handle->renderedFrame.constBits();
    }

    EMSCRIPTEN_KEEPALIVE const std::uint8_t *ugu_frame_pixels(
        const BridgeDocument *handle)
    {
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

    EMSCRIPTEN_KEEPALIVE int ugu_dirty_x(const BridgeDocument *handle)
    {
        return handle->dirty.x();
    }

    EMSCRIPTEN_KEEPALIVE int ugu_dirty_y(const BridgeDocument *handle)
    {
        return handle->dirty.y();
    }

    EMSCRIPTEN_KEEPALIVE int ugu_dirty_width(const BridgeDocument *handle)
    {
        return handle->dirty.width();
    }

    EMSCRIPTEN_KEEPALIVE int ugu_dirty_height(const BridgeDocument *handle)
    {
        return handle->dirty.height();
    }

    // Renders the layer subtree at index as a static thumbnail (frame 0,
    // wobble off), the same picture the desktop layer dock shows. The buffer
    // is premultiplied BGRA like ugu_render_frame and stays valid until the
    // next thumbnail render or close on the same handle.
    EMSCRIPTEN_KEEPALIVE const std::uint8_t *ugu_layer_thumbnail(
        BridgeDocument *handle, int index, double devicePixelRatio)
    {
        const ugurugu::Layer *layer = layerAtIndex(handle, index);
        if (layer == nullptr)
        {
            return nullptr;
        }
        handle->thumbnail = ugurugu::LayerThumbnailRenderer::renderImage(
            handle->controller->document(), *layer, devicePixelRatio);
        if (handle->thumbnail.isNull())
        {
            return nullptr;
        }
        return handle->thumbnail.constBits();
    }

    EMSCRIPTEN_KEEPALIVE int ugu_thumbnail_width(const BridgeDocument *handle)
    {
        return handle->thumbnail.width();
    }

    EMSCRIPTEN_KEEPALIVE int ugu_thumbnail_height(const BridgeDocument *handle)
    {
        return handle->thumbnail.height();
    }

    EMSCRIPTEN_KEEPALIVE int ugu_thumbnail_bytes_per_line(
        const BridgeDocument *handle)
    {
        return static_cast<int>(handle->thumbnail.bytesPerLine());
    }

    // Encodes every animation frame into a GIF, matching the desktop export
    // path (NativeExact pixels, drift-corrected centisecond delays, alpha
    // preserved). The frame set is held in memory like the desktop worker, so
    // documents over the web budget are refused up front instead of risking a
    // tab kill. The pointer stays valid until the next export or close on the
    // same handle; the size comes from ugu_export_size().
    EMSCRIPTEN_KEEPALIVE const std::uint8_t *ugu_export_gif(
        BridgeDocument *handle)
    {
        const ugurugu::Document &document = handle->controller->document();
        constexpr qint64 webFrameSetBudget = 128LL * 1024LL * 1024LL;
        const qint64 frameSetBytes = static_cast<qint64>(document.size.width())
                                     * document.size.height() * 4LL
                                     * document.animationFrames;
        if (frameSetBytes > webFrameSetBudget)
        {
            lastError() =
                QByteArrayLiteral("document is too large for web GIF export");
            return nullptr;
        }

        QVector<QImage> frames;
        frames.reserve(document.animationFrames);
        for (int frame = 0; frame < document.animationFrames; ++frame)
        {
            QImage image = ugurugu::RenderEngine::render(document, frame);
            if (image.isNull())
            {
                lastError() =
                    QByteArrayLiteral("an animation frame could not render");
                return nullptr;
            }
            frames.append(std::move(image));
        }

        const QString path = QStringLiteral("/ugurugu-export.gif");
        QString error;
        if (!ugurugu::GifWriter::write(path,
                frames,
                ugurugu::AnimationExportPolicy::frameDurations(
                    document.animationFrames, document.framesPerSecond, 100),
                &error))
        {
            lastError() = error.toUtf8();
            return nullptr;
        }
        QFile output(path);
        if (!output.open(QIODevice::ReadOnly))
        {
            lastError() = QByteArrayLiteral("encoded GIF could not be read");
            return nullptr;
        }
        handle->exportBytes = output.readAll();
        output.close();
        QFile::remove(path);
        if (handle->exportBytes.isEmpty())
        {
            lastError() = QByteArrayLiteral("encoded GIF is empty");
            return nullptr;
        }
        lastError().clear();
        return reinterpret_cast<const std::uint8_t *>(
            handle->exportBytes.constData());
    }

    EMSCRIPTEN_KEEPALIVE int ugu_export_size(const BridgeDocument *handle)
    {
        return static_cast<int>(handle->exportBytes.size());
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
