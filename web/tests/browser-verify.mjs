// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nyabi (nyattic)

// Drives the built web shell (web/dist) in headless Chromium and verifies the
// IndexedDB recovery loop, the recovery failure surface, and PNG export.
// Run `npm run build` first, then `npm run test:browser`.

import { createServer } from "node:http";
import { readFile } from "node:fs/promises";
import { extname, join, normalize } from "node:path";
import { fileURLToPath } from "node:url";

const distRoot = fileURLToPath(new URL("../dist", import.meta.url));
const chromiumPath = "/Applications/Chromium.app/Contents/MacOS/Chromium";
const brushColor = { red: 29, green: 33, blue: 41 };

const contentTypes = {
    ".html": "text/html; charset=utf-8",
    ".js": "text/javascript",
    ".css": "text/css",
    ".wasm": "application/wasm",
    ".ugu": "application/octet-stream",
    ".svg": "image/svg+xml",
};

function startServer() {
    const server = createServer(async (request, response) => {
        try {
            const url = new URL(request.url, "http://localhost");
            let path = normalize(decodeURIComponent(url.pathname));
            if (path.endsWith("/")) {
                path += "index.html";
            }
            const file = await readFile(join(distRoot, path));
            response.writeHead(200, {
                "Content-Type":
                    contentTypes[extname(path)] ?? "application/octet-stream",
            });
            response.end(file);
        } catch {
            response.writeHead(404);
            response.end();
        }
    });
    return new Promise((resolve) => {
        server.listen(0, "127.0.0.1", () => {
            resolve({
                server,
                origin: `http://127.0.0.1:${server.address().port}`,
            });
        });
    });
}

function countBrushPixels(page) {
    return page.evaluate((color) => {
        const canvas = document.querySelector("canvas");
        const context = canvas.getContext("2d");
        const pixels = context.getImageData(
            0,
            0,
            canvas.width,
            canvas.height,
        ).data;
        let count = 0;
        for (let index = 0; index < pixels.length; index += 4) {
            if (
                pixels[index] === color.red &&
                pixels[index + 1] === color.green &&
                pixels[index + 2] === color.blue
            ) {
                count += 1;
            }
        }
        return count;
    }, brushColor);
}

async function waitForDocumentLoaded(page) {
    await page.waitForFunction(
        () =>
            document
                .querySelector("#status")
                ?.textContent.includes("스키마 v"),
        undefined,
        { timeout: 30000 },
    );
}

async function drawStroke(page) {
    const box = await page.locator("canvas").boundingBox();
    const centerX = box.x + box.width / 2;
    const centerY = box.y + box.height / 2;
    await page.mouse.move(centerX - 80, centerY - 20);
    await page.mouse.down();
    for (let step = 1; step <= 12; step += 1) {
        await page.mouse.move(
            centerX - 80 + step * 14,
            centerY - 20 + step * 5,
        );
    }
    await page.mouse.up();
    await page.waitForFunction(
        () => {
            const canvas = document.querySelector("canvas");
            const data = canvas
                .getContext("2d")
                .getImageData(0, 0, canvas.width, canvas.height).data;
            for (let index = 0; index < data.length; index += 4) {
                if (
                    data[index] === 29 &&
                    data[index + 1] === 33 &&
                    data[index + 2] === 41
                ) {
                    return true;
                }
            }
            return false;
        },
        undefined,
        { timeout: 15000 },
    );
}

const failures = [];

function check(condition, label) {
    if (condition) {
        console.log(`ok: ${label}`);
    } else {
        console.error(`FAIL: ${label}`);
        failures.push(label);
    }
}

const { chromium } = await import("playwright-core");
const { server, origin } = await startServer();
const browser = await chromium.launch({
    executablePath: chromiumPath,
    headless: true,
});

// Scenario 1: draw, autosave, reload, restore, then export the frame as PNG.
{
    const context = await browser.newContext({ acceptDownloads: true });
    const page = await context.newPage();
    await page.goto(`${origin}/?autosave=1`);
    await waitForDocumentLoaded(page);
    check(
        (await page.locator(".recovery-banner").count()) === 0,
        "fresh profile shows no recovery banner",
    );
    check(
        (await countBrushPixels(page)) === 0,
        "canvas starts without brush-colored pixels",
    );

    await drawStroke(page);
    const drawnPixels = await countBrushPixels(page);
    check(drawnPixels > 0, `stroke committed (${drawnPixels} px)`);

    await page.waitForFunction(
        () =>
            document
                .querySelector("#autosave-status")
                ?.textContent.includes("복구 스냅샷 저장됨"),
        undefined,
        { timeout: 20000 },
    );
    check(true, "autosave snapshot reported in status bar");

    await page.reload();
    await page.locator("#recovery-restore").waitFor({ timeout: 30000 });
    check(true, "recovery banner offered after reload");
    await page.locator("#recovery-restore").click();
    await page.waitForFunction(
        (expected) => {
            const canvas = document.querySelector("canvas");
            if (!canvas) {
                return false;
            }
            const data = canvas
                .getContext("2d")
                .getImageData(0, 0, canvas.width, canvas.height).data;
            let count = 0;
            for (let index = 0; index < data.length; index += 4) {
                if (
                    data[index] === 29 &&
                    data[index + 1] === 33 &&
                    data[index + 2] === 41
                ) {
                    count += 1;
                }
            }
            return count === expected;
        },
        drawnPixels,
        { timeout: 30000 },
    );
    check(true, `restored document renders the stroke (${drawnPixels} px)`);

    const downloadPromise = page.waitForEvent("download", { timeout: 20000 });
    await page.locator("#export-png").click();
    const download = await downloadPromise;
    const pngPath = await download.path();
    const pngBytes = await readFile(pngPath);
    check(
        download.suggestedFilename().endsWith(".png"),
        `export filename: ${download.suggestedFilename()}`,
    );
    check(
        pngBytes.length > 8 &&
            pngBytes[0] === 0x89 &&
            pngBytes[1] === 0x50 &&
            pngBytes[2] === 0x4e &&
            pngBytes[3] === 0x47,
        `export produced a PNG (${pngBytes.length} bytes)`,
    );
    await context.close();
}

// Scenario 2: IndexedDB failure must land in the status bar, not crash.
{
    const context = await browser.newContext();
    const page = await context.newPage();
    await page.addInitScript(() => {
        IDBFactory.prototype.open = () => {
            throw new DOMException(
                "QuotaExceededError (simulated)",
                "QuotaExceededError",
            );
        };
    });
    await page.goto(`${origin}/?autosave=1`);
    await waitForDocumentLoaded(page);
    await page.waitForFunction(
        () =>
            document
                .querySelector("#autosave-status")
                ?.textContent.includes("복구 슬롯 확인 실패"),
        undefined,
        { timeout: 15000 },
    );
    check(true, "recovery slot read failure surfaced");

    await drawStroke(page);
    await page.waitForFunction(
        () =>
            document
                .querySelector("#autosave-status")
                ?.textContent.includes("복구 저장 실패"),
        undefined,
        { timeout: 20000 },
    );
    check(true, "snapshot write failure surfaced in status bar");
    await context.close();
}

await browser.close();
server.close();

if (failures.length > 0) {
    console.error(`\n${failures.length} check(s) failed`);
    process.exit(1);
}
console.log("\nOK: recovery and export scenarios verified");
