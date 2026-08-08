import { copyFile, mkdir } from "node:fs/promises";
import { fileURLToPath } from "node:url";

const here = (path) => fileURLToPath(new URL(path, import.meta.url));

await mkdir(here("public/engine"), { recursive: true });
await copyFile(here("../examples/Wave.ugu"), here("public/engine/Wave.ugu"));

// The wasm engine comes from the wasm-release CMake preset. It is absent in CI,
// which builds the shell only to type-check and bundle it.
try {
    for (const artifact of [
        "ugurugu_engine_spike.js",
        "ugurugu_engine_spike.wasm",
    ]) {
        await copyFile(
            here(`../out/build/wasm-release/${artifact}`),
            here(`public/engine/${artifact}`),
        );
    }
    console.log("engine artifacts synced");
} catch (error) {
    if (error.code !== "ENOENT") {
        throw error;
    }
    console.warn("wasm engine not built; the shell will not load a document");
}
