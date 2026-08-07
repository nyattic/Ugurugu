importScripts("ugurugu_engine_spike.js");

const enginePromise = createUguruguEngine();
let documentHandle = 0;

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
    };
}

// The engine hands out premultiplied BGRA rows; putImageData wants straight
// RGBA, so every pixel is swizzled and unpremultiplied here in the worker.
function renderFrame(engine, frame) {
    const pixels = engine._ugu_render_frame(documentHandle, frame);
    if (!pixels) {
        throw new Error(engine.UTF8ToString(engine._ugu_last_error()));
    }
    const width = engine._ugu_frame_width(documentHandle);
    const height = engine._ugu_frame_height(documentHandle);
    const stride = engine._ugu_frame_bytes_per_line(documentHandle);
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

function serializeDocument(engine) {
    const pointer = engine._ugu_serialize(documentHandle);
    if (!pointer) {
        throw new Error(engine.UTF8ToString(engine._ugu_last_error()));
    }
    const size = engine._ugu_serialized_size(documentHandle);
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
            const rendered = renderFrame(engine, event.data.frame);
            postMessage({ id, ok: true, ...rendered }, [rendered.pixels]);
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
