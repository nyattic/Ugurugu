import { createHash } from "node:crypto";
import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";

const modulePath = new URL(
    "../out/build/wasm-release/ugurugu_engine_spike.js",
    import.meta.url,
);
const fixturePath = new URL("../examples/Wave.ugu", import.meta.url);

const { default: createUguruguEngine } = await import(
    fileURLToPath(modulePath)
);
const engine = await createUguruguEngine();

function openDocument(bytes) {
    const pointer = engine._malloc(bytes.length);
    engine.HEAPU8.set(bytes, pointer);
    const handle = engine._ugu_document_open(pointer, bytes.length);
    engine._free(pointer);
    if (!handle) {
        throw new Error(
            `open failed: ${engine.UTF8ToString(engine._ugu_last_error())}`,
        );
    }
    return handle;
}

function renderFrameDigest(handle, frameIndex) {
    const pixels = engine._ugu_render_frame(handle, frameIndex);
    if (!pixels) {
        throw new Error(
            `render failed: ${engine.UTF8ToString(engine._ugu_last_error())}`,
        );
    }
    const width = engine._ugu_frame_width(handle);
    const height = engine._ugu_frame_height(handle);
    const stride = engine._ugu_frame_bytes_per_line(handle);
    const hash = createHash("sha256");
    for (let row = 0; row < height; row += 1) {
        const start = pixels + row * stride;
        hash.update(engine.HEAPU8.subarray(start, start + width * 4));
    }
    return {
        width,
        height,
        digest: hash.digest("hex"),
    };
}

function serializeDocument(handle) {
    const pointer = engine._ugu_serialize(handle);
    if (!pointer) {
        throw new Error(
            `serialize failed: ${engine.UTF8ToString(engine._ugu_last_error())}`,
        );
    }
    const size = engine._ugu_serialized_size(handle);
    return Buffer.from(engine.HEAPU8.subarray(pointer, pointer + size));
}

const sourceBytes = await readFile(fixturePath);
console.log(`schema version: ${engine._ugu_schema_version()}`);
console.log(`fixture: ${sourceBytes.length} bytes`);

const first = openDocument(sourceBytes);
console.log(
    `document: ${engine._ugu_document_width(first)}x` +
        `${engine._ugu_document_height(first)}, ` +
        `${engine._ugu_document_frame_count(first)} frames, ` +
        `${engine._ugu_document_layer_count(first)} layers`,
);

const frameCount = engine._ugu_document_frame_count(first);
const sampledFrames = [0, Math.floor(frameCount / 2), frameCount - 1];
const firstDigests = sampledFrames.map((frame) => {
    const result = renderFrameDigest(first, frame);
    console.log(
        `frame ${frame}: ${result.width}x${result.height} ` +
            `sha256=${result.digest}`,
    );
    return result;
});

const firstSerialized = serializeDocument(first);
const serializedDigest = createHash("sha256")
    .update(firstSerialized)
    .digest("hex");
console.log(
    `serialized: ${firstSerialized.length} bytes sha256=${serializedDigest}`,
);

const second = openDocument(firstSerialized);
const secondDigests = sampledFrames.map((frame) =>
    renderFrameDigest(second, frame),
);
const secondSerialized = serializeDocument(second);

let failed = false;
sampledFrames.forEach((frame, index) => {
    if (firstDigests[index].digest !== secondDigests[index].digest) {
        console.error(`FAIL frame ${frame}: pixel digest changed after round-trip`);
        failed = true;
    }
});
if (!firstSerialized.equals(secondSerialized)) {
    console.error("FAIL serialization is not stable across a round-trip");
    failed = true;
}

engine._ugu_document_close(first);
engine._ugu_document_close(second);

if (failed) {
    process.exit(1);
}
console.log("OK: render digests and serialization stable across round-trip");
