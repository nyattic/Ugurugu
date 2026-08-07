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
        ...historyState(engine),
    };
}

// The engine hands out premultiplied BGRA rows; putImageData wants straight
// RGBA, so every pixel is swizzled and unpremultiplied here in the worker.
function renderFrame(engine, frame) {
    const handle = requireDocument();
    const pixels = engine._ugu_render_frame(handle, frame);
    if (!pixels) {
        throw new Error(engine.UTF8ToString(engine._ugu_last_error()));
    }
    const width = engine._ugu_frame_width(handle);
    const height = engine._ugu_frame_height(handle);
    const stride = engine._ugu_frame_bytes_per_line(handle);
    const rgba = new Uint8ClampedArray(width * height * 4);
    for (let row = 0; row < height; row += 1) {
        const source = engine.HEAPU8.subarray(
            pixels + row * stride,
            pixels + row * stride + width * 4,
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
    return { frame, width, height, pixels: rgba.buffer };
}

function renderReply(engine, id, frame) {
    const rendered = renderFrame(engine, frame);
    postMessage(
        { id, ok: true, ...rendered, ...historyState(engine) },
        [rendered.pixels],
    );
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
        if (type === "open") {
            postMessage({
                id,
                ok: true,
                meta: openDocument(engine, new Uint8Array(event.data.bytes)),
            });
        } else if (type === "render") {
            renderReply(engine, id, event.data.frame);
        } else if (type === "brush") {
            const { red, green, blue, alpha, width, erase } = event.data;
            engine._ugu_set_brush(
                requireDocument(),
                red,
                green,
                blue,
                alpha,
                width,
                erase ? 1 : 0,
            );
            postMessage({ id, ok: true });
        } else if (type === "strokeBegin") {
            const started = engine._ugu_stroke_begin(
                requireDocument(),
                event.data.x,
                event.data.y,
                event.data.pressure,
            );
            if (!started) {
                throw new Error(engine.UTF8ToString(engine._ugu_last_error()));
            }
            renderReply(engine, id, event.data.frame);
        } else if (type === "strokeAppend") {
            const handle = requireDocument();
            const points = event.data.points;
            for (let index = 0; index < points.length; index += 3) {
                engine._ugu_stroke_append(
                    handle,
                    points[index],
                    points[index + 1],
                    points[index + 2],
                );
            }
            renderReply(engine, id, event.data.frame);
        } else if (type === "strokeEnd") {
            engine._ugu_stroke_end(requireDocument());
            renderReply(engine, id, event.data.frame);
        } else if (type === "undo") {
            engine._ugu_undo(requireDocument());
            renderReply(engine, id, event.data.frame);
        } else if (type === "redo") {
            engine._ugu_redo(requireDocument());
            renderReply(engine, id, event.data.frame);
        } else if (type === "serialize") {
            const bytes = serializeDocument(engine);
            postMessage({ id, ok: true, bytes }, [bytes]);
        } else {
            throw new Error(`unknown request: ${type}`);
        }
    } catch (error) {
        postMessage({ id, ok: false, error: String(error) });
    }
};
