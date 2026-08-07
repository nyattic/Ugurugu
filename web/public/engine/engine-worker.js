// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nyabi (nyattic)

importScripts("ugurugu_engine_spike.js");

const enginePromise = createUguruguEngine();
let documentHandle = 0;

function requireDocument() {
    if (!documentHandle) {
        throw new Error("no document open");
    }
    return documentHandle;
}

function historyState(engine) {
    const handle = requireDocument();
    return {
        canUndo: engine._ugu_can_undo(handle) === 1,
        canRedo: engine._ugu_can_redo(handle) === 1,
    };
}

function layerList(engine) {
    const handle = requireDocument();
    const count = engine._ugu_document_layer_count(handle);
    const layers = [];
    for (let index = 0; index < count; index += 1) {
        layers.push({
            index,
            name: engine.UTF8ToString(
                engine._ugu_layer_name(handle, index),
            ),
            group: engine._ugu_layer_kind(handle, index) === 1,
            visible: engine._ugu_layer_visible(handle, index) === 1,
            opacity: engine._ugu_layer_opacity(handle, index),
            active: engine._ugu_layer_is_active(handle, index) === 1,
            depth: engine._ugu_layer_depth(handle, index),
        });
    }
    return layers;
}

function presetList(engine) {
    const handle = requireDocument();
    const count = engine._ugu_brush_preset_count();
    const presets = [];
    for (let index = 0; index < count; index += 1) {
        presets.push({
            index,
            name: engine.UTF8ToString(
                engine._ugu_brush_preset_name(handle, index),
            ),
            defaultSize: engine._ugu_brush_preset_default_size(index),
        });
    }
    return presets;
}

function openDocument(engine, bytes) {
    if (documentHandle) {
        engine._ugu_document_close(documentHandle);
        documentHandle = 0;
    }
    const pointer = engine._malloc(bytes.length);
    engine.HEAPU8.set(bytes, pointer);
    const handle = engine._ugu_document_open(pointer, bytes.length);
    engine._free(pointer);
    if (!handle) {
        throw new Error(engine.UTF8ToString(engine._ugu_last_error()));
    }
    documentHandle = handle;
    return {
        schemaVersion: engine._ugu_schema_version(),
        width: engine._ugu_document_width(handle),
        height: engine._ugu_document_height(handle),
        frameCount: engine._ugu_document_frame_count(handle),
        layerCount: engine._ugu_document_layer_count(handle),
        fps: engine._ugu_document_fps(handle),
        presets: presetList(engine),
        layers: layerList(engine),
        ...historyState(engine),
    };
}

// The engine hands out premultiplied BGRA rows; putImageData wants straight
// RGBA, so the dirty region is swizzled and unpremultiplied here in the
// worker before it crosses to the main thread.
function dirtyRegion(engine) {
    const handle = requireDocument();
    const rect = {
        x: engine._ugu_dirty_x(handle),
        y: engine._ugu_dirty_y(handle),
        width: engine._ugu_dirty_width(handle),
        height: engine._ugu_dirty_height(handle),
    };
    if (rect.width <= 0 || rect.height <= 0) {
        return { rect: { x: 0, y: 0, width: 0, height: 0 }, pixels: null };
    }
    const pixels = engine._ugu_frame_pixels(handle);
    const stride = engine._ugu_frame_bytes_per_line(handle);
    const rgba = new Uint8ClampedArray(rect.width * rect.height * 4);
    for (let row = 0; row < rect.height; row += 1) {
        const sourceStart = pixels + (rect.y + row) * stride + rect.x * 4;
        const source = engine.HEAPU8.subarray(
            sourceStart,
            sourceStart + rect.width * 4,
        );
        const targetOffset = row * rect.width * 4;
        for (let x = 0; x < rect.width; x += 1) {
            const b = source[x * 4];
            const g = source[x * 4 + 1];
            const r = source[x * 4 + 2];
            const a = source[x * 4 + 3];
            const target = targetOffset + x * 4;
            if (a === 0 || a === 255) {
                rgba[target] = r;
                rgba[target + 1] = g;
                rgba[target + 2] = b;
            } else {
                rgba[target] = Math.round((r * 255) / a);
                rgba[target + 1] = Math.round((g * 255) / a);
                rgba[target + 2] = Math.round((b * 255) / a);
            }
            rgba[target + 3] = a;
        }
    }
    return { rect, pixels: rgba.buffer };
}

function regionReply(engine, id) {
    const { rect, pixels } = dirtyRegion(engine);
    const message = {
        id,
        ok: true,
        rect,
        pixels,
        layers: layerList(engine),
        ...historyState(engine),
    };
    postMessage(message, pixels ? [pixels] : []);
}

function fullRender(engine, frame) {
    const handle = requireDocument();
    if (!engine._ugu_render_frame(handle, frame)) {
        throw new Error(engine.UTF8ToString(engine._ugu_last_error()));
    }
}

function serializeDocument(engine) {
    const handle = requireDocument();
    const pointer = engine._ugu_serialize(handle);
    if (!pointer) {
        throw new Error(engine.UTF8ToString(engine._ugu_last_error()));
    }
    const size = engine._ugu_serialized_size(handle);
    return engine.HEAPU8.slice(pointer, pointer + size).buffer;
}

self.onmessage = async (event) => {
    const { id, type } = event.data;
    try {
        const engine = await enginePromise;
        const handleFor = () => requireDocument();
        if (type === "open") {
            postMessage({
                id,
                ok: true,
                meta: openDocument(engine, new Uint8Array(event.data.bytes)),
            });
            return;
        }
        if (type === "serialize") {
            const bytes = serializeDocument(engine);
            postMessage({ id, ok: true, bytes }, [bytes]);
            return;
        }
        if (type === "render") {
            fullRender(engine, event.data.frame);
        } else if (type === "brush") {
            const { red, green, blue, alpha, width, erase } = event.data;
            engine._ugu_set_brush(
                handleFor(),
                red,
                green,
                blue,
                alpha,
                width,
                erase ? 1 : 0,
            );
            postMessage({ id, ok: true });
            return;
        } else if (type === "brushPreset") {
            engine._ugu_set_brush_preset(handleFor(), event.data.index);
            postMessage({ id, ok: true });
            return;
        } else if (type === "stabilization") {
            engine._ugu_set_stabilization(handleFor(), event.data.strength);
            postMessage({ id, ok: true });
            return;
        } else if (type === "strokeBegin") {
            const started = engine._ugu_stroke_begin(
                handleFor(),
                event.data.frame,
                event.data.x,
                event.data.y,
                event.data.pressure,
                event.data.timestamp,
            );
            if (!started) {
                throw new Error(engine.UTF8ToString(engine._ugu_last_error()));
            }
            engine._ugu_stroke_render(handleFor());
        } else if (type === "strokeAppend") {
            const handle = handleFor();
            const points = event.data.points;
            for (let index = 0; index < points.length; index += 4) {
                engine._ugu_stroke_append(
                    handle,
                    points[index],
                    points[index + 1],
                    points[index + 2],
                    points[index + 3],
                );
            }
            engine._ugu_stroke_render(handle);
        } else if (type === "strokeEnd") {
            engine._ugu_stroke_end(handleFor());
        } else if (type === "undo") {
            engine._ugu_undo(handleFor());
            fullRender(engine, event.data.frame);
        } else if (type === "redo") {
            engine._ugu_redo(handleFor());
            fullRender(engine, event.data.frame);
        } else if (type === "layerActivate") {
            engine._ugu_layer_activate(handleFor(), event.data.index);
            fullRender(engine, event.data.frame);
        } else if (type === "layerVisible") {
            engine._ugu_layer_set_visible(
                handleFor(),
                event.data.index,
                event.data.visible ? 1 : 0,
            );
            fullRender(engine, event.data.frame);
        } else if (type === "layerOpacity") {
            engine._ugu_layer_set_opacity(
                handleFor(),
                event.data.index,
                event.data.opacity,
            );
            fullRender(engine, event.data.frame);
        } else if (type === "layerAdd") {
            engine._ugu_layer_add(handleFor());
            fullRender(engine, event.data.frame);
        } else if (type === "layerRemove") {
            engine._ugu_layer_remove(handleFor(), event.data.index);
            fullRender(engine, event.data.frame);
        } else if (type === "layerRename") {
            const utf8 = new TextEncoder().encode(event.data.name + "\0");
            const namePointer = engine._malloc(utf8.length);
            engine.HEAPU8.set(utf8, namePointer);
            engine._ugu_layer_rename(
                handleFor(),
                event.data.index,
                namePointer,
            );
            engine._free(namePointer);
            fullRender(engine, event.data.frame);
        } else if (type === "layerMove") {
            engine._ugu_layer_move(
                handleFor(),
                event.data.index,
                event.data.offset,
            );
            fullRender(engine, event.data.frame);
        } else {
            throw new Error(`unknown request: ${type}`);
        }
        regionReply(engine, id);
    } catch (error) {
        postMessage({ id, ok: false, error: String(error) });
    }
};
