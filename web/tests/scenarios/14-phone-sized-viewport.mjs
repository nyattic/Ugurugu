// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nyabi (nyattic)

import {
    check,
    countBrushPixels,
    drawStroke,
    waitForDocumentLoaded,
} from "../harness.mjs";

// a phone-sized window still has a canvas to draw on. The fixed
// tool and panel columns are wider than the screen on their own, and the
// canvas column was the one flex took the difference out of — it came out zero
// wide, so nothing could be drawn at all.
export default async function run({ browser, origin }) {
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

    const reachable = await page.evaluate(() => {
        const width = document.documentElement.clientWidth;
        const offscreen = [];
        for (const selector of ["header.bar", ".timeline"]) {
            const root = document.querySelector(selector);
            for (const el of root.querySelectorAll("button, input, label")) {
                const box = el.getBoundingClientRect();
                if (box.width === 0 && box.height === 0) {
                    continue;
                }
                if (box.right > width || box.left < 0) {
                    offscreen.push(el.id || el.textContent.trim().slice(0, 20));
                }
            }
        }
        return offscreen;
    });
    check(
        reachable.length === 0,
        `every bar control fits the phone width${
            reachable.length ? ` (off-screen: ${reachable.join(", ")})` : ""
        }`,
    );
    await context.close();
}
