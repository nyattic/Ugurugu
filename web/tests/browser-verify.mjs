// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nyabi (nyattic)

// Drives the built web shell (web/dist) in headless Chromium and verifies the
// IndexedDB recovery loop, the recovery failure surface, exports, the new
// document path, and the view transform. Run `npm run build` first, then
// `npm run test:browser`.
//
// Pixels are always read from #document-surface, the document-resolution
// surface the engine writes to. The visible canvas is the presenter's output
// and is sized in CSS pixels under the current pan and zoom, so it is only
// used for geometry and input.

import { createServer } from "node:http";
import { existsSync } from "node:fs";
import { readFile, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { extname, join, normalize } from "node:path";
import { fileURLToPath } from "node:url";

const distRoot = fileURLToPath(new URL("../dist", import.meta.url));
const brushColor = { red: 29, green: 33, blue: 41 };

const contentTypes = {
    ".html": "text/html; charset=utf-8",
    ".js": "text/javascript",
    ".css": "text/css",
    ".wasm": "application/wasm",
    ".ugu": "application/octet-stream",
    ".svg": "image/svg+xml",
};

// withoutEngine serves everything but the engine loader, which is how a
// blocked or missing wasm artifact looks to the browser.
function startServer({ withoutEngine = false } = {}) {
    const server = createServer(async (request, response) => {
        try {
            const url = new URL(request.url, "http://localhost");
            let path = normalize(decodeURIComponent(url.pathname));
            if (path.endsWith("/")) {
                path += "index.html";
            }
            if (withoutEngine && path.includes("ugurugu_engine_spike.js")) {
                response.writeHead(404);
                response.end();
                return;
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
        const canvas = document.querySelector("#document-surface");
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

async function installPixelCounter(page) {
    await page.addInitScript(() => {
        window.__uguruguBrushCount = () => {
            const canvas = document.querySelector("#document-surface");
            if (!canvas) {
                return -1;
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
            return count;
        };
    });
}

async function waitForDocumentLoaded(page) {
    await page.waitForFunction(
        () =>
            document
                .querySelector("#status")
                ?.textContent.includes("schema v"),
        undefined,
        { timeout: 30000 },
    );
}

async function drawStroke(page) {
    const box = await page.locator("#display-canvas").boundingBox();
    const centerX = box.x + box.width / 2;
    const centerY = box.y + box.height / 2;
    const path = [{ x: centerX - 80, y: centerY - 20 }];
    for (let step = 1; step <= 12; step += 1) {
        path.push({
            x: centerX - 80 + step * 7,
            y: centerY - 20 + step * 2.5,
        });
    }
    await page.mouse.move(path[0].x, path[0].y);
    await page.mouse.down();
    for (const point of path.slice(1)) {
        await page.mouse.move(point.x, point.y);
    }
    // The live preview must appear while the pointer is still down; it once
    // silently regressed to commit-only rendering.
    await page.waitForFunction(hasBrushPixel, undefined, { timeout: 15000 });
    await page.mouse.up();
    await page.waitForFunction(hasBrushPixel, undefined, { timeout: 15000 });
    return path;
}

// Serialised into the page, so it may not close over anything.
function hasBrushPixel() {
    const canvas = document.querySelector("#document-surface");
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
}

async function dragBetween(page, from, to) {
    await page.mouse.move(from.x, from.y);
    await page.mouse.down();
    const steps = 10;
    for (let step = 1; step <= steps; step += 1) {
        await page.mouse.move(
            from.x + ((to.x - from.x) * step) / steps,
            from.y + ((to.y - from.y) * step) / steps,
        );
    }
    await page.mouse.up();
}

// A new document is opaque white, so ink has to be found by color: alpha is
// 255 everywhere.
function rightmostInk(page) {
    return page.evaluate(() => {
        const canvas = document.querySelector("#document-surface");
        const data = canvas
            .getContext("2d")
            .getImageData(0, 0, canvas.width, canvas.height).data;
        let rightmost = -1;
        for (let y = 0; y < canvas.height; y += 1) {
            for (let x = 0; x < canvas.width; x += 1) {
                const index = (y * canvas.width + x) * 4;
                if (
                    data[index] === 29 &&
                    data[index + 1] === 33 &&
                    data[index + 2] === 41
                ) {
                    rightmost = Math.max(rightmost, x);
                }
            }
        }
        return rightmost;
    });
}

// Serialised into the page, so it may not close over anything.
function overlayHasInk() {
    const canvas = document.querySelector("#selection-overlay");
    if (!canvas || canvas.width < 2) {
        return false;
    }
    const data = canvas
        .getContext("2d")
        .getImageData(0, 0, canvas.width, canvas.height).data;
    for (let index = 3; index < data.length; index += 4) {
        if (data[index] > 0) {
            return true;
        }
    }
    return false;
}

// The play control is an icon button, so its state lives in the title rather
// than in visible text.
async function isPlaying(page) {
    const title = await page.locator(".play").getAttribute("title");
    return title?.startsWith("Stop") === true;
}

function firstThumbnailDataUrl(page) {
    return page.evaluate(() => {
        const thumb = document.querySelector("aside .thumb-box canvas");
        if (!thumb || thumb.width <= 1) {
            return null;
        }
        return thumb.toDataURL();
    });
}

async function waitForThumbnails(page) {
    await page.waitForFunction(
        () => {
            const thumb = document.querySelector("aside .thumb-box canvas");
            return thumb !== null && thumb.width > 1;
        },
        undefined,
        { timeout: 20000 },
    );
}

function scratchFile(name) {
    return join(tmpdir(), `ugurugu-verify-${process.pid}-${name}`);
}

// Reads what the compositor actually put on screen at one point. The display
// canvas is WebGL without preserveDrawingBuffer, so it cannot be read back
// directly; the screenshot goes to the page to be decoded instead.
async function screenPixel(page, x, y) {
    const shot = await page.screenshot({
        clip: { x, y, width: 1, height: 1 },
    });
    return page.evaluate(async (encoded) => {
        const image = new Image();
        image.src = `data:image/png;base64,${encoded}`;
        await image.decode();
        const canvas = document.createElement("canvas");
        canvas.width = 1;
        canvas.height = 1;
        const context = canvas.getContext("2d");
        context.drawImage(image, 0, 0);
        return [...context.getImageData(0, 0, 1, 1).data];
    }, shot.toString("base64"));
}

function opaquePixelCount(page, selector) {
    return page.evaluate((id) => {
        const canvas = document.querySelector(id);
        if (!canvas) {
            return -1;
        }
        const data = canvas
            .getContext("2d")
            .getImageData(0, 0, canvas.width, canvas.height).data;
        let opaque = 0;
        for (let index = 3; index < data.length; index += 4) {
            if (data[index] > 0) {
                opaque += 1;
            }
        }
        return opaque;
    }, selector);
}

// Columns of the document surface that carry brush ink, collapsed into runs.
// Two separate strokes have to stay two runs: one run means something bridged
// them, and a run that is only a few columns wide means a stroke lost the
// points that came after its first.
function inkColumnRuns(page) {
    return page.evaluate((color) => {
        const canvas = document.querySelector("#document-surface");
        const data = canvas
            .getContext("2d")
            .getImageData(0, 0, canvas.width, canvas.height).data;
        const runs = [];
        let current = null;
        for (let x = 0; x < canvas.width; x += 1) {
            let inked = false;
            for (let y = 0; y < canvas.height && !inked; y += 1) {
                const index = (y * canvas.width + x) * 4;
                inked =
                    data[index] === color.red &&
                    data[index + 1] === color.green &&
                    data[index + 2] === color.blue;
            }
            if (inked) {
                current ??= { start: x, end: x };
                current.end = x;
            } else if (current) {
                runs.push(current);
                current = null;
            }
        }
        if (current) {
            runs.push(current);
        }
        return runs;
    }, brushColor);
}

// Delays every message on its way to the worker, which is how a document whose
// renders cost more than the gap between two clicks behaves. Scenarios opt in
// by setting window.__slowEngine to a number of milliseconds.
function installWorkerDelay(page) {
    return page.addInitScript(() => {
        window.__sent = 0;
        window.__received = 0;
        const BaseWorker = window.Worker;
        window.Worker = class extends BaseWorker {
            constructor(...args) {
                super(...args);
                const post = this.postMessage.bind(this);
                this.postMessage = (...rest) => {
                    window.__sent += 1;
                    if (window.__slowEngine) {
                        setTimeout(() => post(...rest), window.__slowEngine);
                        return undefined;
                    }
                    return post(...rest);
                };
                this.addEventListener("message", () => {
                    window.__received += 1;
                });
            }
        };
    });
}

async function drainWorker(page) {
    await page.evaluate(() => {
        window.__slowEngine = 0;
    });
    await page.waitForFunction(
        () => window.__sent === window.__received,
        undefined,
        { timeout: 120000 },
    );
}

// A straight horizontal drag with no waiting in between, so the whole stroke is
// handed to the shell before the engine has answered anything.
async function quickStroke(page, box, fromFraction, toFraction, yFraction) {
    const y = box.y + box.height * yFraction;
    const from = box.x + box.width * fromFraction;
    const to = box.x + box.width * toFraction;
    await page.mouse.move(from, y);
    await page.mouse.down();
    for (let step = 1; step <= 6; step += 1) {
        await page.mouse.move(from + ((to - from) * step) / 6, y);
    }
    await page.mouse.up();
}

async function setCanvasRotation(page, degrees) {
    const input = page.locator("#rotation-angle");
    await input.fill(String(degrees));
    await input.press("Enter");
    await input.blur();
    await page.waitForFunction(
        (expected) =>
            Number(document.querySelector("#rotation-angle")?.value) === expected,
        degrees,
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
// playwright-core never downloads a browser, so the executable has to come from
// the environment, a Playwright install, or a local Chromium app bundle.
function resolveChromiumPath() {
    const candidates = [
        process.env.UGURUGU_CHROMIUM_PATH,
        safeExecutablePath(),
        "/Applications/Chromium.app/Contents/MacOS/Chromium",
        "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
    ];
    for (const candidate of candidates) {
        if (candidate && existsSync(candidate)) {
            return candidate;
        }
    }
    throw new Error(
        "No Chromium found. Run `npx playwright install chromium` or set " +
            "UGURUGU_CHROMIUM_PATH.",
    );
}

function safeExecutablePath() {
    try {
        return chromium.executablePath();
    } catch {
        return null;
    }
}

const { server, origin } = await startServer();
const browser = await chromium.launch({
    executablePath: resolveChromiumPath(),
    headless: true,
});

// Scenario 1: draw, autosave, reload, restore, then export the frame as PNG.
{
    const context = await browser.newContext({ acceptDownloads: true });
    const page = await context.newPage();
    await installPixelCounter(page);
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
    await waitForThumbnails(page);
    const thumbnailBeforeStroke = await firstThumbnailDataUrl(page);
    check(
        thumbnailBeforeStroke !== null,
        "layer thumbnail rendered after load",
    );

    const strokePath = await drawStroke(page);
    const drawnPixels = await countBrushPixels(page);
    check(drawnPixels > 0, `stroke committed (${drawnPixels} px)`);

    await page.waitForFunction(
        (before) => {
            const thumb = document.querySelector("aside .thumb-box canvas");
            return thumb !== null && thumb.width > 1
                && thumb.toDataURL() !== before;
        },
        thumbnailBeforeStroke,
        { timeout: 20000 },
    );
    check(true, "layer thumbnail refreshed after the stroke");

    await page.waitForFunction(
        () =>
            document
                .querySelector("#autosave-status")
                ?.textContent.includes("Recovery snapshot saved"),
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
            const canvas = document.querySelector("#document-surface");
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

    const gifDownloadPromise = page.waitForEvent("download", {
        timeout: 60000,
    });
    await page.locator("#export-gif").click();
    const gifDownload = await gifDownloadPromise;
    const gifBytes = await readFile(await gifDownload.path());
    check(
        gifDownload.suggestedFilename().endsWith(".gif"),
        `gif export filename: ${gifDownload.suggestedFilename()}`,
    );
    check(
        gifBytes.length > 6 &&
            gifBytes.slice(0, 6).toString("latin1") === "GIF89a",
        `animated GIF exported (${gifBytes.length} bytes)`,
    );

    await waitForThumbnails(page);
    check(true, "layer thumbnails rendered for the restored document");

    check(
        (await page
            .locator('#recent-colors .swatch[title="#1d2129"]')
            .count()) === 1,
        "recent color history recorded the committed brush color",
    );

    const wheelBox = await page.locator("#color-wheel").boundingBox();
    const wheelCenterX = wheelBox.x + wheelBox.width / 2;
    const wheelCenterY = wheelBox.y + wheelBox.height / 2;
    await page.mouse.click(
        wheelCenterX + wheelBox.width * 0.42,
        wheelCenterY,
    );
    await page.mouse.click(
        wheelCenterX + wheelBox.width * 0.24,
        wheelCenterY - wheelBox.height * 0.24,
    );
    check(
        (await page.locator('input[type="color"]').inputValue()) === "#ff0000",
        "color wheel ring and field pick pure red",
    );

    // Sampling a point the pointer itself passed through keeps this check
    // independent of the current pan and zoom.
    await page.locator("#tool-eyedropper").click();
    const pickTarget = strokePath[Math.floor(strokePath.length / 2)];
    await page.mouse.click(pickTarget.x, pickTarget.y);
    check(
        (await page.locator('input[type="color"]').inputValue()) === "#1d2129",
        "eyedropper picked the stroke color from the canvas",
    );

    const zoomBefore = await page.locator("#zoom-fit").textContent();
    await page.locator(".zoom-controls button:last-child").click();
    const zoomAfter = await page.locator("#zoom-fit").textContent();
    check(
        zoomBefore !== zoomAfter,
        `zoom in changed the scale (${zoomBefore.trim()} -> ` +
            `${zoomAfter.trim()})`,
    );
    await page.locator("#zoom-fit").click();
    check(
        (await page.locator("#zoom-fit").textContent()) === zoomBefore,
        "zoom to fit restored the initial scale",
    );

    // The stroke has to stay put in document space across a zoom, which is
    // what a wrong presenter transform would break.
    await page.keyboard.press("Control+Equal");
    const zoomedPixels = await countBrushPixels(page);
    check(
        zoomedPixels === drawnPixels,
        `zooming does not change document pixels (${zoomedPixels} px)`,
    );
    await page.keyboard.press("Control+Digit0");

    await page.keyboard.press("e");
    check(
        (await page.locator("#eraser-preset").count()) === 1,
        "the E shortcut selects the eraser and reveals its presets",
    );
    check(
        (await page.locator("#eraser-preset option").count()) > 0,
        "eraser presets come from the engine catalog",
    );
    await page.keyboard.press("b");

    // The restored document carries the stroke as content with no history, so
    // undo needs a stroke drawn in this session to work on.
    const beforeSecondStroke = await countBrushPixels(page);
    await page.mouse.move(pickTarget.x, pickTarget.y + 60);
    await page.mouse.down();
    for (let step = 1; step <= 10; step += 1) {
        await page.mouse.move(pickTarget.x + step * 6, pickTarget.y + 60);
    }
    await page.mouse.up();
    await page.waitForFunction(
        (before) => window.__uguruguBrushCount?.() > before,
        beforeSecondStroke,
        { timeout: 20000 },
    );
    await page.keyboard.press("Control+z");
    await page.waitForFunction(
        (expected) => window.__uguruguBrushCount?.() === expected,
        beforeSecondStroke,
        { timeout: 20000 },
    );
    check(true, "the Ctrl+Z shortcut undid the stroke");

    // BrushSettings::antialiasing defaults off and no preset sets it, so
    // without this toggle the shell could only ever commit aliased strokes.
    // Aliased edges also leave hairline seams where wobbled segments meet.
    const alias = (checked) =>
        page.evaluate((want) => {
            const box = document.querySelector("#brush-antialiasing");
            if (box.checked !== want) {
                box.click();
            }
            return box.checked;
        }, checked);
    check((await alias(true)) === true, "antialiasing can be turned on");
    const smoothEdgesBefore = await page.evaluate(() => {
        const canvas = document.querySelector("#document-surface");
        const data = canvas
            .getContext("2d")
            .getImageData(0, 0, canvas.width, canvas.height).data;
        let partial = 0;
        for (let index = 0; index < data.length; index += 4) {
            const value = data[index];
            if (value > 60 && value < 200) {
                partial += 1;
            }
        }
        return partial;
    });
    await drawStroke(page);
    const smoothEdgesAfter = await page.evaluate(() => {
        const canvas = document.querySelector("#document-surface");
        const data = canvas
            .getContext("2d")
            .getImageData(0, 0, canvas.width, canvas.height).data;
        let partial = 0;
        for (let index = 0; index < data.length; index += 4) {
            const value = data[index];
            if (value > 60 && value < 200) {
                partial += 1;
            }
        }
        return partial;
    });
    check(
        smoothEdgesAfter > smoothEdgesBefore,
        `antialiased stroke added ${smoothEdgesAfter - smoothEdgesBefore} ` +
            `partially covered edge pixels`,
    );

    // Preset selection replaces the engine's brush template wholesale, which
    // used to silently drop the toggle: cycling presets or visiting the
    // eraser left the box checked but committed aliased strokes.
    const smoothEdgeCensus = () => {
        const canvas = document.querySelector("#document-surface");
        const data = canvas
            .getContext("2d")
            .getImageData(0, 0, canvas.width, canvas.height).data;
        let partial = 0;
        for (let index = 0; index < data.length; index += 4) {
            const value = data[index];
            if (value > 60 && value < 200) {
                partial += 1;
            }
        }
        return partial;
    };
    const drawOffsetStroke = async (offsetY) => {
        const box = await page.locator("#display-canvas").boundingBox();
        const startX = box.x + box.width / 2 - 80;
        const startY = box.y + box.height / 2 + offsetY;
        await page.mouse.move(startX, startY);
        await page.mouse.down();
        for (let step = 1; step <= 12; step += 1) {
            await page.mouse.move(startX + step * 7, startY + step * 2.5);
        }
        await page.mouse.up();
    };
    const expectSmootherAfter = async (baseline, label) => {
        await page.waitForFunction(
            (edges) => {
                const canvas = document.querySelector("#document-surface");
                const data = canvas
                    .getContext("2d")
                    .getImageData(0, 0, canvas.width, canvas.height).data;
                let partial = 0;
                for (let index = 0; index < data.length; index += 4) {
                    const value = data[index];
                    if (value > 60 && value < 200) {
                        partial += 1;
                    }
                }
                return partial > edges;
            },
            baseline,
            { timeout: 15000 },
        );
        check(true, label);
    };
    await page.locator("#brush-preset").selectOption("1");
    await page.locator("#brush-preset").selectOption("0");
    const beforePresetCycle = await page.evaluate(smoothEdgeCensus);
    await drawOffsetStroke(40);
    await expectSmootherAfter(
        beforePresetCycle,
        "antialiasing survives a brush preset cycle",
    );
    await page.locator("#tool-eraser").click();
    await page.locator("#tool-brush").click();
    const beforeEraserRoundTrip = await page.evaluate(smoothEdgeCensus);
    await drawOffsetStroke(80);
    await expectSmootherAfter(
        beforeEraserRoundTrip,
        "antialiasing survives an eraser round-trip",
    );
    await page.keyboard.press("Control+z");
    await alias(false);

    // addStroke refuses a stroke when any point falls outside the canvas, so
    // a drag that leaves the viewport used to lose the whole line.
    const beforeExit = await countBrushPixels(page);
    const canvasBox = await page.locator("#display-canvas").boundingBox();
    const startX = canvasBox.x + canvasBox.width / 2;
    const startY = canvasBox.y + canvasBox.height / 2 + 60;
    await page.mouse.move(startX, startY);
    await page.mouse.down();
    for (let step = 1; step <= 8; step += 1) {
        // Runs well past the right edge of the document and back inside.
        await page.mouse.move(startX + step * 90, startY + step * 4);
    }
    await page.mouse.move(startX + 40, startY + 30);
    await page.mouse.up();
    await page.waitForFunction(
        (before) => window.__uguruguBrushCount?.() > before,
        beforeExit,
        { timeout: 20000 },
    );
    check(true, "a stroke that leaves the canvas still commits");
    check(
        !(await page.locator("#status").textContent())?.includes("Error"),
        "leaving the canvas reports no error",
    );
    await page.keyboard.press("Control+z");

    // Drawing pauses the wobble but must not switch playback off, or the user
    // has to restart it after every stroke. CanvasWidget::advanceFrame does
    // the same by skipping frames instead of clearing m_animating.
    await page.locator(".play").click();
    check(await isPlaying(page), "playback started");
    await drawStroke(page);
    check(await isPlaying(page), "playback survives a stroke");

    // Rendering a playback frame mid-stroke used to wipe the live stroke
    // and replace the image the incremental compositor patches into, which
    // flashed garbage until the pointer was released.
    await page.locator("#animate-while-drawing").check();
    const brushCountBeforeWobbleDraw = await page.evaluate(() =>
        window.__uguruguBrushCount?.(),
    );
    // Clear of the stroke above, so the commit adds a whole line rather than
    // the sliver two wobbles of the same path happen to disagree on. Playback
    // shares the request queue, so the commit can land several frames after
    // the pointer went up and has to be waited for rather than sampled.
    await drawOffsetStroke(-40);
    await page.waitForFunction(
        (before) => window.__uguruguBrushCount?.() > before,
        brushCountBeforeWobbleDraw,
        { timeout: 20000 },
    );
    check(true, "a stroke drawn while wobbling commits");
    check(
        !(await page.locator("#status").textContent())?.includes("Error"),
        "wobble-while-drawing reports no error",
    );
    check(await isPlaying(page), "playback survives a wobbling stroke");
    await page.locator("#animate-while-drawing").uncheck();
    await page.locator(".play").click();
    await context.close();
}

// Scenario 2: a new document honours the web memory policy and can be drawn on.
{
    const context = await browser.newContext();
    const page = await context.newPage();
    await page.goto(`${origin}/`);
    await waitForDocumentLoaded(page);

    await page.locator("#new-document").click();
    await page.locator("#new-document-width").fill("640");
    await page.locator("#new-document-height").fill("480");
    await page.locator("#new-document-confirm").click();
    // "Creating a … document" is also a status, so wait for the surface.
    await page.waitForFunction(
        () => document.querySelector("#document-surface")?.height === 480,
        undefined,
        { timeout: 30000 },
    );
    const size = await page.evaluate(() => {
        const canvas = document.querySelector("#document-surface");
        return { width: canvas.width, height: canvas.height };
    });
    check(
        size.width === 640 && size.height === 480,
        `new document surface is ${size.width}x${size.height}`,
    );
    check(
        (await countBrushPixels(page)) === 0,
        "new document starts empty",
    );

    await drawStroke(page);
    const newDocumentPixels = await countBrushPixels(page);
    check(
        newDocumentPixels > 0,
        `stroke committed on the new document (${newDocumentPixels} px)`,
    );

    // The clamp is the policy gate the report asked for; the engine itself
    // would accept up to 4096.
    await page.locator("#new-document").click();
    const maximum = await page
        .locator("#new-document-width")
        .getAttribute("max");
    check(
        Number(maximum) === 2048,
        `new document dialog caps the edge at ${maximum}`,
    );
    await page.locator("#new-document-width").fill("9000");
    await page.locator("#new-document-confirm").click();
    await page.waitForFunction(
        () => document.querySelector("#document-surface")?.width !== 640,
        undefined,
        { timeout: 60000 },
    );
    const clamped = await page.evaluate(
        () => document.querySelector("#document-surface").width,
    );
    check(clamped === 2048, `oversized request clamped to ${clamped}`);
    await context.close();
}

// Scenario 3: IndexedDB failure must land in the status bar, not crash.
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
                ?.textContent.includes("Could not read the recovery slot"),
        undefined,
        { timeout: 15000 },
    );
    check(true, "recovery slot read failure surfaced");

    await drawStroke(page);
    await page.waitForFunction(
        () =>
            document
                .querySelector("#autosave-status")
                ?.textContent.includes("Recovery save failed"),
        undefined,
        { timeout: 20000 },
    );
    check(true, "snapshot write failure surfaced in status bar");
    await context.close();
}

// Scenario 4: the selection and fill tools — lasso, wand and bucket — plus the
// marching-ants overlay that carries the engine's selection back to the shell.
// A 640x480 document keeps every coordinate below in document space.
{
    const width = 640;
    const height = 480;
    const context = await browser.newContext();
    const page = await context.newPage();
    await installPixelCounter(page);
    await page.goto(`${origin}/`);
    await waitForDocumentLoaded(page);

    await page.locator("#new-document").click();
    await page.locator("#new-document-width").fill(String(width));
    await page.locator("#new-document-height").fill(String(height));
    await page.locator("#new-document-confirm").click();
    await page.waitForFunction(
        (expected) =>
            document.querySelector("#document-surface")?.height === expected,
        height,
        { timeout: 30000 },
    );

    const box = await page.locator("#display-canvas").boundingBox();
    // The document is centred in a viewport that is usually larger than it, so
    // screen coordinates have to be derived rather than guessed. Fit never
    // magnifies, and nothing here pans, so the centre stays the document's.
    const zoom =
        Number(
            (await page.locator("#zoom-fit").textContent())
                ?.trim()
                .replace("%", ""),
        ) / 100;
    const at = (documentX, documentY) => ({
        x: box.x + box.width / 2 + (documentX - width / 2) * zoom,
        y: box.y + box.height / 2 + (documentY - height / 2) * zoom,
    });

    await page.locator("#tool-lasso").click();
    await page.locator("#selection-shape-rectangle").click();
    check(
        (await page.locator("#selection-actions").count()) === 0,
        "no selection actions before anything is selected",
    );

    await dragBetween(page, at(250, 190), at(390, 290));
    await page.locator("#selection-actions").waitFor({ timeout: 20000 });
    check(true, "a rectangle lasso drag produced a selection");

    // The ants are drawn from contours the engine traced, so ink on the
    // overlay proves the outline survived the round trip.
    await page.waitForFunction(overlayHasInk, undefined, { timeout: 20000 });
    check(true, "the selection overlay drew marching ants");

    await page.locator("#selection-fill").click();
    await page.waitForFunction(
        () => window.__uguruguBrushCount?.() > 0,
        undefined,
        { timeout: 20000 },
    );
    const filled = await countBrushPixels(page);
    check(
        filled > 13000 && filled <= 140 * 100,
        `filling the 140x100 selection painted ${filled} px inside it`,
    );

    await page.keyboard.press("Control+d");
    await page.waitForFunction(
        () => document.querySelector("#selection-actions") === null,
        undefined,
        { timeout: 20000 },
    );
    check(true, "Ctrl+D deselected");

    // A stroke that starts inside a selection must stop at its edge, which is
    // the clipMask the bridge attaches to every stroke.
    await dragBetween(page, at(200, 140), at(440, 340));
    await page.locator("#selection-actions").waitFor({ timeout: 20000 });
    await page.locator("#tool-brush").click();
    await page.mouse.move(at(320, 240).x, at(320, 240).y);
    await page.mouse.down();
    for (let step = 1; step <= 12; step += 1) {
        const point = at(320 + step * 25, 240);
        await page.mouse.move(point.x, point.y);
    }
    await page.mouse.up();
    await page.waitForFunction(
        (before) => window.__uguruguBrushCount?.() > before,
        filled,
        { timeout: 20000 },
    );
    const clippedRight = await rightmostInk(page);
    check(
        clippedRight > 400 && clippedRight < 460,
        `the stroke stopped at the selection edge (rightmost ink x ` +
            `${clippedRight}, edge 439)`,
    );

    await page.locator("#selection-delete").click();
    await page.waitForFunction(
        () => window.__uguruguBrushCount?.() === 0,
        undefined,
        { timeout: 30000 },
    );
    check(true, "deleting the selection removed everything inside it");
    check(
        (await page.locator("#selection-actions").count()) === 0,
        "deleting the selection also clears it",
    );

    // Nothing is drawn now, so an alpha-boundary flood has no walls to stop
    // at and covers the whole canvas.
    await page.locator("#tool-bucket").click();
    const corner = at(20, 20);
    await page.mouse.click(corner.x, corner.y);
    await page.waitForFunction(
        (expected) => window.__uguruguBrushCount?.() === expected,
        width * height,
        { timeout: 30000 },
    );
    check(true, `the bucket flooded the empty layer (${width * height} px)`);
    check(
        !(await page.locator("#status").textContent())?.includes("code"),
        "the bucket reported no error",
    );

    await page.keyboard.press("Control+z");
    await page.waitForFunction(
        () => window.__uguruguBrushCount?.() === 0,
        undefined,
        { timeout: 30000 },
    );
    check(true, "undo took the bucket fill back");

    // Paint mode commits the lassoed area as a fill instead of selecting it.
    await page.locator("#tool-lasso").click();
    await page.locator("#lasso-mode-paint").click();
    await page.locator("#selection-shape-ellipse").click();
    await dragBetween(page, at(60, 60), at(200, 180));
    await page.waitForFunction(
        () => window.__uguruguBrushCount?.() > 0,
        undefined,
        { timeout: 20000 },
    );
    check(
        (await page.locator("#selection-actions").count()) === 0,
        "lasso paint mode fills without leaving a selection behind",
    );

    // The wand needs a seed the flood can start from, so it goes where no
    // paint has reached.
    await page.locator("#tool-wand").click();
    const empty = at(600, 440);
    await page.mouse.click(empty.x, empty.y);
    await page.locator("#selection-actions").waitFor({ timeout: 20000 });
    check(true, "the wand selected the area around the painted shape");

    // The rail letters are the desktop's, and every one of them has a button.
    for (const [key, id] of [
        ["l", "tool-lasso"],
        ["w", "tool-wand"],
        ["g", "tool-bucket"],
        ["b", "tool-brush"],
    ]) {
        await page.keyboard.press(key);
        check(
            (await page.locator(`#${id}`).getAttribute("aria-pressed")) ===
                "true",
            `the ${key.toUpperCase()} shortcut selects ${id}`,
        );
    }

    await context.close();
}

// Scenario 5: a failed open must not take the open document with it.
{
    const context = await browser.newContext();
    const page = await context.newPage();
    await installPixelCounter(page);
    await page.goto(origin);
    await waitForDocumentLoaded(page);
    await drawStroke(page);
    const drawn = await countBrushPixels(page);

    const corrupt = scratchFile("corrupt.ugu");
    await writeFile(corrupt, "this is not a ugu document");
    await page.locator('input[type="file"]').setInputFiles(corrupt);
    await page.waitForFunction(
        () =>
            document
                .querySelector("#status")
                ?.textContent.startsWith("Open failed"),
        undefined,
        { timeout: 20000 },
    );
    check(true, "a corrupt file reports an open failure");
    check(
        await page.locator("#save-document").isEnabled(),
        "the document that was open survives a failed open",
    );
    check(
        (await countBrushPixels(page)) === drawn,
        "a failed open leaves the artwork untouched",
    );

    // Still live: another stroke commits on the document that was kept.
    const box = await page.locator("#display-canvas").boundingBox();
    await dragBetween(
        page,
        { x: box.x + 60, y: box.y + box.height - 60 },
        { x: box.x + 220, y: box.y + box.height - 60 },
    );
    await page.waitForFunction(
        (before) => window.__uguruguBrushCount?.() > before,
        drawn,
        { timeout: 20000 },
    );
    check(true, "drawing still works after a failed open");
    await context.close();
}

// Scenario 6: the engine refuses to paint where the artist cannot see, the way
// CanvasWidget::beginStroke does.
{
    const context = await browser.newContext();
    const page = await context.newPage();
    await installPixelCounter(page);
    await page.goto(origin);
    await waitForDocumentLoaded(page);

    await page.locator('aside li input[type="checkbox"]').first().uncheck();
    const box = await page.locator("#display-canvas").boundingBox();
    const middle = { x: box.x + box.width / 2, y: box.y + box.height / 2 };
    await dragBetween(
        page,
        { x: middle.x - 60, y: middle.y },
        { x: middle.x + 60, y: middle.y },
    );
    await page.waitForFunction(
        () =>
            document.querySelector("#status")?.textContent.includes("hidden"),
        undefined,
        { timeout: 20000 },
    );
    check(true, "drawing on a hidden layer is refused with a reason");

    await page.locator('aside li input[type="checkbox"]').first().check();
    await page.waitForFunction(
        () => window.__uguruguBrushCount?.() === 0,
        undefined,
        { timeout: 20000 },
    );
    check(true, "the refused stroke was never committed");

    // The same keypress that deletes a selection must stay quiet when there is
    // nothing selected, so the status bar has to read the same afterwards.
    const statusBefore = await page.locator("#status").textContent();
    await page.keyboard.press("Backspace");
    await page.waitForTimeout(500);
    check(
        (await page.locator("#status").textContent()) === statusBefore,
        "Delete with no selection reports nothing",
    );
    await context.close();
}

// Scenario 7: brush and eraser keep their own size, and a preset default above
// the old 64 ceiling is reachable.
{
    const context = await browser.newContext();
    const page = await context.newPage();
    await page.goto(origin);
    await waitForDocumentLoaded(page);

    await page.locator("#brush-preset").selectOption({ label: "Ink Pen" });
    const brushSize = await page.locator("#brush-size").inputValue();
    await page.locator("#tool-eraser").click();
    await page.locator("#eraser-preset").selectOption({ label: "Soft" });
    const eraserSize = await page.locator("#eraser-size").inputValue();
    check(
        eraserSize !== brushSize,
        `the eraser has its own size (brush ${brushSize}, eraser ${eraserSize})`,
    );
    await page.locator("#tool-brush").click();
    check(
        (await page.locator("#brush-size").inputValue()) === brushSize,
        "picking an eraser preset leaves the brush size alone",
    );

    await page
        .locator("#brush-preset")
        .selectOption({ label: "Droplet Spray" });
    check(
        (await page.locator("#brush-size").inputValue()) === "72",
        "a preset default above the old 64 ceiling is reachable",
    );
    await context.close();
}

// Scenario 8: a translucent document has to reach the screen as straight
// alpha. Blending into the premultipliedAlpha: false drawing buffer used to
// square the alpha, which turned a half-opaque layer nearly black.
{
    const context = await browser.newContext({ acceptDownloads: true });
    const page = await context.newPage();
    await page.goto(origin);
    await waitForDocumentLoaded(page);

    const download = page.waitForEvent("download");
    await page.locator("#save-document").click();
    const savedPath = scratchFile("saved.ugu");
    await (await download).saveAs(savedPath);
    const saved = JSON.parse(await readFile(savedPath, "utf8"));
    saved.canvas.background = "#00ffffff";
    const transparentPath = scratchFile("transparent.ugu");
    await writeFile(transparentPath, JSON.stringify(saved));

    await page.locator('input[type="file"]').setInputFiles(transparentPath);
    await page.waitForFunction(
        () =>
            document
                .querySelector("#status")
                ?.textContent.includes("transparent"),
        undefined,
        { timeout: 30000 },
    );

    await page.evaluate(() => {
        const input = document.querySelector('input[type="color"]');
        input.value = "#ff0000";
        input.dispatchEvent(new Event("input", { bubbles: true }));
    });
    await page.locator("#tool-bucket").click();
    const box = await page.locator("#display-canvas").boundingBox();
    const middle = {
        x: Math.round(box.x + box.width / 2),
        y: Math.round(box.y + box.height / 2),
    };
    await page.mouse.click(middle.x, middle.y);
    await page.evaluate(() => {
        const slider = document.querySelector(
            ".opacity-controls input[type=range]",
        );
        slider.value = "50";
        slider.dispatchEvent(new Event("change", { bubbles: true }));
    });
    await page.waitForFunction(
        () => {
            const canvas = document.querySelector("#document-surface");
            const pixel = canvas
                .getContext("2d")
                .getImageData(
                    Math.floor(canvas.width / 2),
                    Math.floor(canvas.height / 2),
                    1,
                    1,
                ).data;
            return pixel[0] === 255 && pixel[3] > 100 && pixel[3] < 155;
        },
        undefined,
        { timeout: 30000 },
    );
    check(true, "a half-opaque red layer reaches the document surface");

    const shown = await screenPixel(page, middle.x, middle.y);
    // Straight alpha over the dark viewport backdrop lands near 145; the
    // squared-alpha bug put it near 56.
    check(
        shown[0] > 120 && shown[0] < 175,
        `the screen shows the layer at its real opacity (red ${shown[0]})`,
    );
    await context.close();
}

// Scenario 9: losing the WebGL context must fall back to a canvas that can
// actually take a 2D context, not to the same element.
{
    const context = await browser.newContext();
    const page = await context.newPage();
    await installPixelCounter(page);
    await page.goto(origin);
    await waitForDocumentLoaded(page);
    const presenterStatus = await page
        .locator("#presenter-status")
        .textContent();
    if (presenterStatus?.includes("WebGL 2")) {
        await page.locator("#new-document").click();
        await page.locator("#new-document-width").fill("320");
        await page.locator("#new-document-height").fill("160");
        await page.locator("#new-document-confirm").click();
        await page.waitForFunction(
            () => document.querySelector("#document-surface")?.width === 320,
            undefined,
            { timeout: 30000 },
        );
        await setCanvasRotation(page, 45);
        await page.locator("#zoom-fit").click();
        await page.evaluate(() => {
            const gl = document
                .querySelector("#display-canvas")
                .getContext("webgl2");
            gl?.getExtension("WEBGL_lose_context")?.loseContext();
        });
        await page.waitForSelector("#display-software", { timeout: 20000 });
        check(true, "a context loss creates the software display canvas");
        await page.waitForFunction(
            () =>
                document
                    .querySelector("#presenter-status")
                    ?.textContent.includes("Canvas 2D"),
            undefined,
            { timeout: 20000 },
        );
        check(true, "the status bar stops claiming WebGL after the loss");
        check(
            (await opaquePixelCount(page, "#display-software")) > 0,
            "the software display still shows the document",
        );
        const box = await page.locator("#display-canvas").boundingBox();
        const center = { x: box.x + box.width / 2, y: box.y + box.height / 2 };
        const centerPixel = await screenPixel(page, center.x, center.y);
        const outsidePixel = await screenPixel(page, center.x + 150, center.y);
        check(
            centerPixel[0] > 235 &&
                centerPixel[1] > 235 &&
                centerPixel[2] > 235,
            "the software fallback keeps the rotated document at its centre",
        );
        check(
            outsidePixel[0] < 120 &&
                outsidePixel[1] < 120 &&
                outsidePixel[2] < 120,
            "the software fallback clips outside the rotated document",
        );
    } else {
        console.log("skip: this browser has no WebGL 2 to lose");
    }
    await context.close();
}

// Scenario 10: playback must not queue renders it cannot keep up with. The
// worker is delayed here to stand in for a document whose full render costs
// more than one frame interval.
{
    const context = await browser.newContext();
    const page = await context.newPage();
    await installWorkerDelay(page);
    await page.goto(origin);
    await waitForDocumentLoaded(page);
    await page.evaluate(() => {
        window.__slowEngine = 250;
    });
    await page.locator(".play").click();
    check(await isPlaying(page), "playback started against a slow engine");
    await page.waitForTimeout(4000);
    await page.locator(".play").click();
    const stoppedAt = Date.now();
    await page.waitForFunction(
        () => window.__sent === window.__received,
        undefined,
        { timeout: 120000 },
    );
    const drain = Date.now() - stoppedAt;
    check(
        drain < 3000,
        `stopping playback leaves no render backlog (drained in ${drain} ms)`,
    );
    await context.close();
}

// Scenario 11: an engine artifact that never arrives has to say so.
{
    const blocked = await startServer({ withoutEngine: true });
    const context = await browser.newContext();
    const page = await context.newPage();
    await page.goto(blocked.origin);
    await page.waitForFunction(
        () =>
            /failed|could not/i.test(
                document.querySelector("#status")?.textContent ?? "",
            ),
        undefined,
        { timeout: 30000 },
    );
    check(true, "a missing engine artifact surfaces instead of hanging");
    await context.close();
    blocked.server.close();
}

// Scenario 12: two strokes drawn back to back, faster than the engine answers,
// have to stay two strokes. The point batch used to be read when the queued
// append finally ran rather than when the pointer produced it, so a second
// stroke starting first either emptied the buffer under the open stroke or
// handed it its own points.
{
    const context = await browser.newContext();
    const page = await context.newPage();
    await installWorkerDelay(page);
    await page.goto(origin);
    await waitForDocumentLoaded(page);
    await page.evaluate(() => {
        window.__slowEngine = 400;
    });

    const box = await page.locator("#display-canvas").boundingBox();
    await quickStroke(page, box, 0.1, 0.35, 0.5);
    await quickStroke(page, box, 0.65, 0.9, 0.5);
    await drainWorker(page);
    await page.waitForFunction(
        () => document.querySelector("#undo")?.disabled === false,
        undefined,
        { timeout: 30000 },
    );

    const runs = await inkColumnRuns(page);
    check(
        runs.length === 2,
        `two quick strokes stay two strokes (${runs.length} ink run(s))`,
    );
    const widths = runs.map((run) => run.end - run.start);
    check(
        widths.every((width) => width > 20),
        `neither quick stroke lost its tail (widths ${widths.join(", ")})`,
    );
    await context.close();
}

// Scenario 13: layer commands name the layer, not the row. Two deletes issued
// before the first is answered used to remove the row index twice, taking a
// second, unrelated layer with it.
{
    const context = await browser.newContext();
    const page = await context.newPage();
    await installWorkerDelay(page);
    await page.goto(origin);
    await waitForDocumentLoaded(page);

    await page.locator("#layer-add").click();
    await page.locator("#layer-add").click();
    await page.waitForFunction(
        () => document.querySelectorAll("aside ul li").length === 3,
        undefined,
        { timeout: 30000 },
    );

    await page.evaluate(() => {
        window.__slowEngine = 400;
    });
    await page.locator("#layer-remove").click();
    await page.locator("#layer-remove").click();
    await drainWorker(page);
    await page.waitForTimeout(250);

    const remaining = await page.evaluate(
        () => document.querySelectorAll("aside ul li").length,
    );
    check(
        remaining === 2,
        `a repeated delete removes one layer, not two (${remaining} left)`,
    );
    await context.close();
}

// Scenario 14: a phone-sized window still has a canvas to draw on. The fixed
// tool and panel columns are wider than the screen on their own, and the
// canvas column was the one flex took the difference out of — it came out zero
// wide, so nothing could be drawn at all.
{
    const context = await browser.newContext({
        viewport: { width: 390, height: 844 },
    });
    const page = await context.newPage();
    await page.goto(origin);
    await waitForDocumentLoaded(page);

    const box = await page.locator("#display-canvas").boundingBox();
    check(
        box.width >= 200 && box.height >= 200,
        `phone-sized canvas is ${Math.round(box.width)}x` +
            `${Math.round(box.height)}`,
    );
    await drawStroke(page);
    const drawn = await countBrushPixels(page);
    check(drawn > 0, `a phone-sized canvas takes a stroke (${drawn} px)`);
    await context.close();
}

// Scenario 15: canvas rotation is a view-only transform. Controls, shortcuts,
// free rotation, wheel input, presentation, and inverse pointer mapping must
// agree without changing the document pixels.
{
    const context = await browser.newContext({
        viewport: { width: 1180, height: 820 },
    });
    const page = await context.newPage();
    await installPixelCounter(page);
    await page.goto(origin);
    await waitForDocumentLoaded(page);

    check(
        (await page.locator("#rotation-angle").inputValue()) === "0",
        "canvas rotation starts at 0 degrees",
    );
    await page.locator("#rotate-right").click();
    check(
        (await page.locator("#rotation-angle").inputValue()) === "5",
        "the rotate-right control adds 5 degrees",
    );
    await page.keyboard.press("-");
    check(
        (await page.locator("#rotation-angle").inputValue()) === "0",
        "the minus shortcut rotates left by 5 degrees",
    );
    await page.keyboard.press("Shift+6");
    check(
        (await page.locator("#rotation-angle").inputValue()) === "5",
        "the caret shortcut rotates right by 5 degrees",
    );
    await page.locator("#rotation-reset").click();

    const zoomBeforeShortcut = await page.locator("#zoom-fit").textContent();
    await page.keyboard.press("Control+-");
    check(
        (await page.locator("#rotation-angle").inputValue()) === "0",
        "Ctrl-minus zooms without rotating the canvas",
    );
    check(
        (await page.locator("#zoom-fit").textContent()) !== zoomBeforeShortcut,
        "Ctrl-minus still changes the zoom",
    );
    await page.keyboard.press("Control+0");

    const canvasBox = await page.locator("#display-canvas").boundingBox();
    const center = {
        x: canvasBox.x + canvasBox.width / 2,
        y: canvasBox.y + canvasBox.height / 2,
    };
    await page.keyboard.down("Shift");
    await page.mouse.move(center.x, center.y);
    await page.mouse.wheel(0, -100);
    await page.keyboard.up("Shift");
    check(
        (await page.locator("#rotation-angle").inputValue()) === "5",
        "Shift-wheel rotates the canvas by 5 degrees",
    );
    await page.locator("#rotation-reset").click();

    await page.keyboard.down("Shift");
    await page.keyboard.down("Space");
    await page.mouse.move(center.x, center.y);
    await page.mouse.down();
    await page.mouse.move(center.x + 40, center.y);
    await page.mouse.up();
    await page.keyboard.up("Space");
    await page.keyboard.up("Shift");
    check(
        Number(await page.locator("#rotation-angle").inputValue()) === 20,
        "Shift-Space drag rotates at 0.5 degrees per pixel",
    );
    check(
        (await countBrushPixels(page)) === 0,
        "rotating the canvas does not paint into the document",
    );

    await page.locator("#new-document").click();
    await page.locator("#new-document-width").fill("320");
    await page.locator("#new-document-height").fill("160");
    await page.locator("#new-document-confirm").click();
    await page.waitForFunction(
        () => document.querySelector("#document-surface")?.width === 320,
        undefined,
        { timeout: 30000 },
    );
    await setCanvasRotation(page, 45);
    await page.locator("#zoom-fit").click();
    check(
        (await page.locator("#rotation-angle").inputValue()) === "45",
        "fit-to-window preserves the rotation",
    );

    const smallCanvasBox = await page.locator("#display-canvas").boundingBox();
    const smallCenter = {
        x: smallCanvasBox.x + smallCanvasBox.width / 2,
        y: smallCanvasBox.y + smallCanvasBox.height / 2,
    };
    const centerPixel = await screenPixel(page, smallCenter.x, smallCenter.y);
    const outsideRotatedQuad = await screenPixel(
        page,
        smallCenter.x + 150,
        smallCenter.y,
    );
    check(
        centerPixel[0] > 235 && centerPixel[1] > 235 && centerPixel[2] > 235,
        "the rotated WebGL quad shows the document at its centre",
    );
    check(
        outsideRotatedQuad[0] < 120 &&
            outsideRotatedQuad[1] < 120 &&
            outsideRotatedQuad[2] < 120,
        "the WebGL presenter clips outside the rotated document quad",
    );

    await setCanvasRotation(page, 37);
    const radians = (37 * Math.PI) / 180;
    const target = {
        documentX: 240,
        documentY: 80,
        screenX: smallCenter.x + Math.cos(radians) * 80,
        screenY: smallCenter.y + Math.sin(radians) * 80,
    };
    await page.mouse.click(target.screenX, target.screenY);
    await page.waitForFunction(hasBrushPixel, undefined, { timeout: 15000 });
    const inkCenter = await page.evaluate((color) => {
        const canvas = document.querySelector("#document-surface");
        const data = canvas
            .getContext("2d")
            .getImageData(0, 0, canvas.width, canvas.height).data;
        let totalX = 0;
        let totalY = 0;
        let count = 0;
        for (let y = 0; y < canvas.height; y += 1) {
            for (let x = 0; x < canvas.width; x += 1) {
                const index = (y * canvas.width + x) * 4;
                if (
                    data[index] === color.red &&
                    data[index + 1] === color.green &&
                    data[index + 2] === color.blue
                ) {
                    totalX += x;
                    totalY += y;
                    count += 1;
                }
            }
        }
        return { x: totalX / count, y: totalY / count, count };
    }, brushColor);
    check(
        inkCenter.count > 0 &&
            Math.abs(inkCenter.x - target.documentX) < 8 &&
            Math.abs(inkCenter.y - target.documentY) < 8,
        `rotated pointer mapping lands at document ${inkCenter.x.toFixed(1)},` +
            `${inkCenter.y.toFixed(1)}`,
    );
    await context.close();
}

// Scenario 16: two touch points translate, pinch, and twist as one transform.
// The document point under the old midpoint must follow the new midpoint.
{
    const context = await browser.newContext({
        viewport: { width: 1180, height: 820 },
        hasTouch: true,
    });
    const page = await context.newPage();
    await installPixelCounter(page);
    await page.goto(origin);
    await waitForDocumentLoaded(page);
    await page.locator("#new-document").click();
    await page.locator("#new-document-width").fill("320");
    await page.locator("#new-document-height").fill("160");
    await page.locator("#new-document-confirm").click();
    await page.waitForFunction(
        () => document.querySelector("#document-surface")?.width === 320,
        undefined,
        { timeout: 30000 },
    );
    await page.locator("#tool-lasso").click();

    const canvasBox = await page.locator("#display-canvas").boundingBox();
    const oldCenter = {
        x: canvasBox.x + canvasBox.width / 2,
        y: canvasBox.y + canvasBox.height / 2,
    };
    const nextCenter = { x: oldCenter.x + 40, y: oldCenter.y + 25 };
    const radians = Math.PI / 6;
    const axis = { x: Math.cos(radians) * 70, y: Math.sin(radians) * 70 };
    const session = await context.newCDPSession(page);
    const point = (x, y, id) => ({
        x,
        y,
        id,
        radiusX: 1,
        radiusY: 1,
        force: 1,
    });
    await session.send("Input.dispatchTouchEvent", {
        type: "touchStart",
        touchPoints: [
            point(oldCenter.x - 50, oldCenter.y, 1),
            point(oldCenter.x + 50, oldCenter.y, 2),
        ],
    });
    await session.send("Input.dispatchTouchEvent", {
        type: "touchMove",
        touchPoints: [
            point(nextCenter.x - axis.x, nextCenter.y - axis.y, 1),
            point(nextCenter.x + axis.x, nextCenter.y + axis.y, 2),
        ],
    });
    await session.send("Input.dispatchTouchEvent", {
        type: "touchEnd",
        touchPoints: [],
    });

    check(
        (await page.locator("#rotation-angle").inputValue()) === "30",
        "a two-finger twist rotates the canvas by 30 degrees",
    );
    check(
        (await page.locator("#zoom-fit").textContent()) === "140%",
        "the same two-finger gesture pinches to 140 percent",
    );
    check(
        (await countBrushPixels(page)) === 0,
        "the two-finger gesture does not paint into the document",
    );

    await page.locator("#tool-brush").click();
    await page.mouse.click(nextCenter.x, nextCenter.y);
    await page.waitForFunction(hasBrushPixel, undefined, { timeout: 15000 });
    const inkCenter = await page.evaluate((color) => {
        const canvas = document.querySelector("#document-surface");
        const data = canvas
            .getContext("2d")
            .getImageData(0, 0, canvas.width, canvas.height).data;
        let totalX = 0;
        let totalY = 0;
        let count = 0;
        for (let y = 0; y < canvas.height; y += 1) {
            for (let x = 0; x < canvas.width; x += 1) {
                const index = (y * canvas.width + x) * 4;
                if (
                    data[index] === color.red &&
                    data[index + 1] === color.green &&
                    data[index + 2] === color.blue
                ) {
                    totalX += x;
                    totalY += y;
                    count += 1;
                }
            }
        }
        return { x: totalX / count, y: totalY / count, count };
    }, brushColor);
    check(
        inkCenter.count > 0 &&
            Math.abs(inkCenter.x - 160) < 8 &&
            Math.abs(inkCenter.y - 80) < 8,
        `the touch midpoint keeps document ${inkCenter.x.toFixed(1)},` +
            `${inkCenter.y.toFixed(1)} anchored while it moves`,
    );
    await context.close();
}

await browser.close();
server.close();

if (failures.length > 0) {
    console.error(`\n${failures.length} check(s) failed`);
    process.exit(1);
}
console.log("\nOK: recovery and export scenarios verified");
