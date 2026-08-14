// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nyabi (nyattic)

#include "render/LayerThumbnailRenderer.hpp"
#include "wasm/BridgeDocument.hpp"

#include <QString>

using namespace ugurugu::wasm;

extern "C"
{
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

    // The layer's stable identity. Indexes shift under every add, remove and
    // move, so a shell that queues those operations has to name the layer it
    // meant rather than the row it saw.
    EMSCRIPTEN_KEEPALIVE const char *ugu_layer_id(
        BridgeDocument *handle, int index)
    {
        const ugurugu::Layer *layer = layerAtIndex(handle, index);
        if (layer == nullptr)
        {
            return "";
        }
        handle->scratchLayerId =
            layer->id.toString(QUuid::WithoutBraces).toUtf8();
        return handle->scratchLayerId.constData();
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

    EMSCRIPTEN_KEEPALIVE int ugu_layer_reference(
        const BridgeDocument *handle, int index)
    {
        const ugurugu::Layer *layer = layerAtIndex(handle, index);
        return layer != nullptr && layer->reference ? 1 : 0;
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
            invalidateSplit(handle);
        }
    }

    EMSCRIPTEN_KEEPALIVE void ugu_layer_set_visible(
        BridgeDocument *handle, int index, int visible)
    {
        if (const ugurugu::Layer *layer = layerAtIndex(handle, index))
        {
            handle->controller->setLayerVisible(layer->id, visible != 0);
            invalidateSplit(handle);
        }
    }

    // Marks a paint layer as a flood-fill reference, the source the wand and
    // the bucket read when their reference option is "marked layers".
    EMSCRIPTEN_KEEPALIVE void ugu_layer_set_reference(
        BridgeDocument *handle, int index, int reference)
    {
        if (const ugurugu::Layer *layer = layerAtIndex(handle, index))
        {
            handle->controller->setLayerReference(layer->id, reference != 0);
        }
    }

    EMSCRIPTEN_KEEPALIVE void ugu_layer_set_opacity(
        BridgeDocument *handle, int index, double opacity)
    {
        if (const ugurugu::Layer *layer = layerAtIndex(handle, index))
        {
            handle->controller->setLayerOpacity(layer->id, opacity);
            invalidateSplit(handle);
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
        invalidateSplit(handle);
    }

    EMSCRIPTEN_KEEPALIVE void ugu_layer_remove(
        BridgeDocument *handle, int index)
    {
        if (const ugurugu::Layer *layer = layerAtIndex(handle, index))
        {
            handle->controller->removeLayer(layer->id);
            invalidateSplit(handle);
        }
    }

    EMSCRIPTEN_KEEPALIVE void ugu_layer_rename(
        BridgeDocument *handle, int index, const char *name)
    {
        if (const ugurugu::Layer *layer = layerAtIndex(handle, index))
        {
            handle->controller->renameLayer(layer->id, QString::fromUtf8(name));
            invalidateSplit(handle);
        }
    }

    EMSCRIPTEN_KEEPALIVE void ugu_layer_move(
        BridgeDocument *handle, int index, int offset)
    {
        if (const ugurugu::Layer *layer = layerAtIndex(handle, index))
        {
            handle->controller->moveLayer(layer->id, offset);
            invalidateSplit(handle);
        }
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
}
