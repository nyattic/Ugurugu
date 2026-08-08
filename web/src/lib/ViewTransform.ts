// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nyabi (nyattic)

export interface ViewState {
    // Document pixels per CSS pixel.
    scale: number;
    // Document point pinned to the centre of the viewport.
    centerX: number;
    centerY: number;
}

export const minimumScale = 0.05;
export const maximumScale = 32;

export interface Viewport {
    width: number;
    height: number;
}

export function fitToViewport(
    document: { width: number; height: number },
    viewport: Viewport,
): ViewState {
    const scale =
        document.width <= 0 || document.height <= 0
            ? 1
            : Math.min(
                  viewport.width / document.width,
                  viewport.height / document.height,
                  maximumScale,
              );
    return {
        scale: Math.max(minimumScale, scale),
        centerX: document.width / 2,
        centerY: document.height / 2,
    };
}

export function clampScale(scale: number): number {
    return Math.min(maximumScale, Math.max(minimumScale, scale));
}

export function toDocument(
    view: ViewState,
    viewport: Viewport,
    viewportX: number,
    viewportY: number,
): { x: number; y: number } {
    return {
        x: view.centerX + (viewportX - viewport.width / 2) / view.scale,
        y: view.centerY + (viewportY - viewport.height / 2) / view.scale,
    };
}

export function toViewport(
    view: ViewState,
    viewport: Viewport,
    documentX: number,
    documentY: number,
): { x: number; y: number } {
    return {
        x: viewport.width / 2 + (documentX - view.centerX) * view.scale,
        y: viewport.height / 2 + (documentY - view.centerY) * view.scale,
    };
}

// Keeps the document point under the cursor pinned while the scale changes,
// which is what makes wheel and pinch zoom feel anchored rather than jumpy.
export function zoomAround(
    view: ViewState,
    viewport: Viewport,
    viewportX: number,
    viewportY: number,
    nextScale: number,
): ViewState {
    const scale = clampScale(nextScale);
    const anchor = toDocument(view, viewport, viewportX, viewportY);
    return {
        scale,
        centerX: anchor.x - (viewportX - viewport.width / 2) / scale,
        centerY: anchor.y - (viewportY - viewport.height / 2) / scale,
    };
}

export function pan(
    view: ViewState,
    deltaViewportX: number,
    deltaViewportY: number,
): ViewState {
    return {
        scale: view.scale,
        centerX: view.centerX - deltaViewportX / view.scale,
        centerY: view.centerY - deltaViewportY / view.scale,
    };
}
