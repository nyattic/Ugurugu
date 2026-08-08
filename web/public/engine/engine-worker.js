// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nyabi (nyattic)

importScripts("ugurugu_engine_spike.js");

// Must match ugu_abi_version() in src/wasm/EngineBridge.cpp. A stale artifact
// under public/engine used to surface as "… is not a function" deep inside an
// unrelated call; refusing here names the real problem instead.
const expectedAbiVersion = 2;

const enginePromise = createUguruguEngine().then((engine) => {
    const version = engine._ugu_abi_version?.();
    if (version !== expectedAbiVersion) {
        throw new Error(
            `engine ABI ${version ?? "unknown"} does not match the shell's ` +
                `${expectedAbiVersion}; rebuild the wasm-release preset and ` +
                `run npm run sync-engine`,
        );
    }
    return engine;
});
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

function eraserPresetList(engine) {
    const handle = requireDocument();
    const count = engine._ugu_eraser_preset_count();
    const presets = [];
    for (let index = 0; index < count; index += 1) {
        presets.push({
            index,
            name: engine.UTF8ToString(
                engine._ugu_eraser_preset_name(handle, index),
            ),
            defaultSize: engine._ugu_eraser_preset_default_size(index),
        });
    }
    return presets;
}

function engineError(engine) {
    return new Error(
        `${engine.UTF8ToString(engine._ugu_last_error())} ` +
            `(code ${engine._ugu_last_error_code()})`,
    );
}

function adoptDocument(engine, handle, undoLimit) {
    documentHandle = handle;
    if (undoLimit > 0) {
        engine._ugu_set_undo_limit(handle, undoLimit);
    }
    return {
        abiVersion: engine._ugu_abi_version(),
        schemaVersion: engine._ugu_schema_version(),
        width: engine._ugu_document_width(handle),
        height: engine._ugu_document_height(handle),
        frameCount: engine._ugu_document_frame_count(handle),
        layerCount: engine._ugu_document_layer_count(handle),
        fps: engine._ugu_document_fps(handle),
        presets: presetList(engine),
        eraserPresets: eraserPresetList(engine),
        layers: layerList(engine),
        ...historyState(engine),
    };
}

function closeDocument(engine) {
    if (documentHandle) {
        engine._ugu_document_close(documentHandle);
        documentHandle = 0;
    }
}

function createDocument(engine, width, height, undoLimit) {
    closeDocument(engine);
    const handle = engine._ugu_document_new(width, height);
    if (!handle) {
        throw engineError(engine);
    }
    return adoptDocument(engine, handle, undoLimit);
}

function openDocument(engine, bytes, undoLimit) {
    closeDocument(engine);
    const pointer = engine._malloc(bytes.length);
    engine.HEAPU8.set(bytes, pointer);
    const handle = engine._ugu_document_open(pointer, bytes.length);
    engine._free(pointer);
    if (!handle) {
        throw engineError(engine);
    }
    return adoptDocument(engine, handle, undoLimit);
}

// The engine hands out premultiplied BGRA rows; putImageData wants straight
// RGBA, so pixel regions are swizzled and unpremultiplied here in the worker
// before they cross to the main thread.
function bgraToRgba(engine, pointer, offsetX, offsetY, width, height, stride) {
    const rgba = new Uint8ClampedArray(width * height * 4);
    for (let row = 0; row < height; row += 1) {
        const sourceStart = pointer + (offsetY + row) * stride + offsetX * 4;
        const source = engine.HEAPU8.subarray(
            sourceStart,
            sourceStart + width * 4,
        );
        const targetOffset = row * width * 4;
        for (let x = 0; x < width; x += 1) {
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
    return rgba;
}

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
    const rgba = bgraToRgba(
        engine,
        engine._ugu_frame_pixels(handle),
        rect.x,
        rect.y,
        rect.width,
        rect.height,
        engine._ugu_frame_bytes_per_line(handle),
    );
    return { rect, pixels: rgba.buffer };
}

function layerThumbnails(engine, devicePixelRatio) {
    const handle = requireDocument();
    const count = engine._ugu_document_layer_count(handle);
    const thumbnails = [];
    const transfers = [];
    for (let index = 0; index < count; index += 1) {
        const pointer = engine._ugu_layer_thumbnail(
            handle,
            index,
            devicePixelRatio,
        );
        if (!pointer) {
            thumbnails.push({ index, width: 0, height: 0, pixels: null });
            continue;
        }
        const width = engine._ugu_thumbnail_width(handle);
        const height = engine._ugu_thumbnail_height(handle);
        const rgba = bgraToRgba(
            engine,
            pointer,
            0,
            0,
            width,
            height,
            engine._ugu_thumbnail_bytes_per_line(handle),
        );
        thumbnails.push({ index, width, height, pixels: rgba.buffer });
        transfers.push(rgba.buffer);
    }
    return { thumbnails, transfers };
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
        throw engineError(engine);
    }
}

function serializeDocument(engine) {
    const handle = requireDocument();
    const pointer = engine._ugu_serialize(handle);
    if (!pointer) {
        throw engineError(engine);
    }
    const size = engine._ugu_serialized_size(handle);
    return engine.HEAPU8.slice(pointer, pointer + size).buffer;
}

function exportGif(engine) {
    const handle = requireDocument();
    const pointer = engine._ugu_export_gif(handle);
    if (!pointer) {
        throw engineError(engine);
    }
    const size = engine._ugu_export_size(handle);
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
                meta: openDocument(
                    engine,
                    new Uint8Array(event.data.bytes),
                    event.data.undoLimit,
                ),
            });
            return;
        }
        if (type === "create") {
            postMessage({
                id,
                ok: true,
                meta: createDocument(
                    engine,
                    event.data.width,
                    event.data.height,
                    event.data.undoLimit,
                ),
            });
            return;
        }
        if (type === "serialize") {
            const bytes = serializeDocument(engine);
            postMessage({ id, ok: true, bytes }, [bytes]);
            return;
        }
        if (type === "exportGif") {
            const bytes = exportGif(engine);
            postMessage({ id, ok: true, bytes }, [bytes]);
            return;
        }
        if (type === "layerThumbnails") {
            const { thumbnails, transfers } = layerThumbnails(
                engine,
                event.data.devicePixelRatio,
            );
            postMessage({ id, ok: true, thumbnails }, transfers);
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
        } else if (type === "eraserPreset") {
            engine._ugu_set_eraser_preset(handleFor(), event.data.index);
            postMessage({ id, ok: true });
            return;
        } else if (type === "brushAntialiasing") {
            engine._ugu_set_brush_antialiasing(
                handleFor(),
                event.data.antialiasing ? 1 : 0,
            );
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
                throw engineError(engine);
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
