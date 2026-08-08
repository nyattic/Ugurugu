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
import { readFile } from "node:fs/promises";
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

await browser.close();
server.close();

if (failures.length > 0) {
    console.error(`\n${failures.length} check(s) failed`);
    process.exit(1);
}
console.log("\nOK: recovery and export scenarios verified");
