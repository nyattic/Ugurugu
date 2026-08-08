// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nyabi (nyattic)

export interface ShortcutActions {
    undo: () => void;
    redo: () => void;
    save: () => void;
    open: () => void;
    newDocument: () => void;
    selectBrush: () => void;
    selectEraser: () => void;
    selectEyedropper: () => void;
    adjustBrushSize: (delta: number) => void;
    zoomBy: (factor: number) => void;
    zoomToFit: () => void;
    zoomToActualSize: () => void;
    stepFrame: (delta: number) => void;
    togglePlayback: () => void;
}

// Browser- and OS-owned chords (Cmd/Ctrl+W, +L, +T, reload, native zoom) are
// deliberately left alone: inside the itch.io iframe the app cannot assume it
// owns them, so every shortcut here also has a visible control.
export function handleShortcut(
    event: KeyboardEvent,
    actions: ShortcutActions,
): boolean {
    const target = event.target as HTMLElement | null;
    if (
        target &&
        (target.isContentEditable ||
            ["INPUT", "TEXTAREA", "SELECT"].includes(target.tagName))
    ) {
        return false;
    }
    const accelerator = event.metaKey || event.ctrlKey;
    const key = event.key.toLowerCase();

    if (accelerator) {
        switch (key) {
            case "z":
                if (event.shiftKey) {
                    actions.redo();
                } else {
                    actions.undo();
                }
                return true;
            case "y":
                actions.redo();
                return true;
            case "s":
                actions.save();
                return true;
            case "o":
                actions.open();
                return true;
            case "n":
                actions.newDocument();
                return true;
            case "0":
                actions.zoomToFit();
                return true;
            case "1":
                actions.zoomToActualSize();
                return true;
            case "=":
            case "+":
                actions.zoomBy(1.25);
                return true;
            case "-":
                actions.zoomBy(1 / 1.25);
                return true;
            default:
                return false;
        }
    }

    if (event.altKey) {
        return false;
    }

    switch (key) {
        case "b":
            actions.selectBrush();
            return true;
        case "e":
            actions.selectEraser();
            return true;
        case "i":
            actions.selectEyedropper();
            return true;
        case "[":
            actions.adjustBrushSize(-1);
            return true;
        case "]":
            actions.adjustBrushSize(1);
            return true;
        case "arrowleft":
            actions.stepFrame(-1);
            return true;
        case "arrowright":
            actions.stepFrame(1);
            return true;
        case "enter":
            actions.togglePlayback();
            return true;
        default:
            return false;
    }
}
