import { svelte } from "@sveltejs/vite-plugin-svelte";
import { defineConfig } from "vite";

export default defineConfig({
    // itch.io serves the game from a per-project CDN subdirectory, so every
    // asset reference has to be relative. tools/check_itchio_package.mjs
    // fails the build if an absolute one slips back in.
    base: "./",
    plugins: [svelte()],
});
