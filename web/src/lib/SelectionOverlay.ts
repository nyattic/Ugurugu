// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nyabi (nyattic)

import { toViewport } from "./ViewTransform";
import type { ViewState, Viewport } from "./ViewTransform";
import type { SelectionContour } from "./EngineClient";

export interface DragShape {
    shape: "freehand" | "rectangle" | "ellipse";
    points: number[];
}

// Draws over the display canvas, never into the document: the committed
// selection as marching ants, plus whatever the pointer is drawing right now.
// Two passes, light under dark, so the outline reads over any artwork —
// canvas_detail::drawSelectionPath does the same on the desktop.
export class SelectionOverlay {
    #canvas: HTMLCanvasElement;
    #context: CanvasRenderingContext2D | null;
    #contours: SelectionContour[] = [];
    #drag: DragShape | null = null;
    #dashOffset = 0;
    #frame = 0;

    constructor(canvas: HTMLCanvasElement) {
        this.#canvas = canvas;
        this.#context = canvas.getContext("2d");
    }

    setContours(contours: SelectionContour[]) {
        this.#contours = contours;
    }

    setDrag(drag: DragShape | null) {
        this.#drag = drag;
    }

    get hasContent(): boolean {
        return this.#contours.length > 0 || this.#drag !== null;
    }

    resize(width: number, height: number, devicePixelRatio: number) {
        const pixelWidth = Math.max(1, Math.round(width * devicePixelRatio));
        const pixelHeight = Math.max(1, Math.round(height * devicePixelRatio));
        if (
            this.#canvas.width === pixelWidth &&
            this.#canvas.height === pixelHeight
        ) {
            return;
        }
        this.#canvas.width = pixelWidth;
        this.#canvas.height = pixelHeight;
    }

    // Advances the ants. Returns true when something is on screen, so the
    // caller can stop its animation loop once the selection goes away.
    advance(): boolean {
        this.#frame += 1;
        if (this.#frame % 3 === 0) {
            this.#dashOffset = (this.#dashOffset + 1) % 8;
        }
        return this.hasContent;
    }

    draw(view: ViewState) {
        const context = this.#context;
        if (!context) {
            return;
        }
        const cssWidth = this.#canvas.clientWidth || this.#canvas.width;
        const cssHeight = this.#canvas.clientHeight || this.#canvas.height;
        const ratio = this.#canvas.width / Math.max(1, cssWidth);
        context.setTransform(ratio, 0, 0, ratio, 0, 0);
        context.clearRect(0, 0, cssWidth, cssHeight);
        if (!this.hasContent) {
            return;
        }

        const viewport: Viewport = { width: cssWidth, height: cssHeight };
        const path = new Path2D();
        for (const contour of this.#contours) {
            for (let index = 0; index + 1 < contour.length; index += 2) {
                const point = toViewport(
                    view,
                    viewport,
                    contour[index] ?? 0,
                    contour[index + 1] ?? 0,
                );
                if (index === 0) {
                    path.moveTo(point.x, point.y);
                } else {
                    path.lineTo(point.x, point.y);
                }
            }
            path.closePath();
        }
        this.#appendDrag(path, view, viewport);

        context.lineJoin = "miter";
        context.setLineDash([]);
        context.lineWidth = 2.4;
        context.strokeStyle = "rgba(255, 255, 255, 0.92)";
        context.stroke(path);

        context.lineWidth = 1.2;
        context.setLineDash([4, 4]);
        context.lineDashOffset = this.#dashOffset;
        context.strokeStyle = "rgba(18, 19, 22, 0.96)";
        context.stroke(path);
        context.setLineDash([]);
    }

    #appendDrag(path: Path2D, view: ViewState, viewport: Viewport) {
        const drag = this.#drag;
        if (!drag || drag.points.length < 4) {
            return;
        }
        const at = (index: number) =>
            toViewport(
                view,
                viewport,
                drag.points[index * 2] ?? 0,
                drag.points[index * 2 + 1] ?? 0,
            );
        if (drag.shape === "freehand") {
            const start = at(0);
            path.moveTo(start.x, start.y);
            for (let index = 1; index < drag.points.length / 2; index += 1) {
                const point = at(index);
                path.lineTo(point.x, point.y);
            }
            path.closePath();
            return;
        }
        const anchor = at(0);
        const current = at(drag.points.length / 2 - 1);
        const x = Math.min(anchor.x, current.x);
        const y = Math.min(anchor.y, current.y);
        const width = Math.abs(current.x - anchor.x);
        const height = Math.abs(current.y - anchor.y);
        if (drag.shape === "rectangle") {
            path.rect(x, y, width, height);
            return;
        }
        path.ellipse(
            x + width / 2,
            y + height / 2,
            width / 2,
            height / 2,
            0,
            0,
            Math.PI * 2,
        );
    }
}
