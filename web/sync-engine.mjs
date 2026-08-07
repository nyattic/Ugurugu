import { copyFile, mkdir } from "node:fs/promises";
import { fileURLToPath } from "node:url";

const here = (path) => fileURLToPath(new URL(path, import.meta.url));

await mkdir(here("public/engine"), { recursive: true });
const copies = [
    ["../out/build/wasm-release/ugurugu_engine_spike.js", "public/engine/ugurugu_engine_spike.js"],
    ["../out/build/wasm-release/ugurugu_engine_spike.wasm", "public/engine/ugurugu_engine_spike.wasm"],
    ["../examples/Wave.ugu", "public/engine/Wave.ugu"],
];
for (const [source, target] of copies) {
    await copyFile(here(source), here(target));
}
console.log("engine artifacts synced");
