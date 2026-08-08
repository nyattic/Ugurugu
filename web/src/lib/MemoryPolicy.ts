// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nyabi (nyattic)

// The numbers come from docs/web-port-progress.md, which derived them from
// the wasm heap measurements: an open document costs roughly the file size
// times 4.4, plus one render surface per frame (16 MiB at 2048x2048), plus a
// transient copy the size of the file while serializing. They are deliberately
// stricter than the engine's own DocumentLimits so the browser tab fails with
// a message instead of being killed.

export interface MemoryProfile {
    name: "desktop" | "mobile";
    maximumCanvasEdge: number;
    defaultCanvasWidth: number;
    defaultCanvasHeight: number;
    importWarningBytes: number;
    importRejectionBytes: number;
    undoLimit: number;
}

const desktop: MemoryProfile = {
    name: "desktop",
    maximumCanvasEdge: 2048,
    defaultCanvasWidth: 1024,
    defaultCanvasHeight: 768,
    importWarningBytes: 32 * 1024 * 1024,
    importRejectionBytes: 64 * 1024 * 1024,
    undoLimit: 64,
};

const mobile: MemoryProfile = {
    name: "mobile",
    maximumCanvasEdge: 1024,
    defaultCanvasWidth: 1024,
    defaultCanvasHeight: 768,
    importWarningBytes: 16 * 1024 * 1024,
    importRejectionBytes: 64 * 1024 * 1024,
    undoLimit: 32,
};

// Coarse on purpose. A wrong guess only changes limits and the wording of a
// warning, so a pointer/viewport heuristic beats user-agent sniffing here.
export function detectMemoryProfile(): MemoryProfile {
    const override = new URLSearchParams(window.location.search).get("profile");
    if (override === "mobile") {
        return mobile;
    }
    if (override === "desktop") {
        return desktop;
    }
    const coarsePointer =
        window.matchMedia?.("(pointer: coarse)").matches ?? false;
    const smallViewport = Math.min(window.screen.width, window.screen.height) <=
        820;
    return coarsePointer && smallViewport ? mobile : desktop;
}

export function clampCanvasEdge(value: number, profile: MemoryProfile): number {
    if (!Number.isFinite(value)) {
        return profile.defaultCanvasWidth;
    }
    return Math.min(profile.maximumCanvasEdge, Math.max(1, Math.round(value)));
}

export type ImportVerdict =
    | { allowed: true; warning: string | null }
    | { allowed: false; reason: string };

export function formatMebibytes(bytes: number): string {
    return `${(bytes / (1024 * 1024)).toFixed(1)} MiB`;
}

export function checkImportSize(
    bytes: number,
    profile: MemoryProfile,
): ImportVerdict {
    if (bytes > profile.importRejectionBytes) {
        return {
            allowed: false,
            reason:
                `파일이 ${formatMebibytes(bytes)}로 웹 한도 ` +
                `${formatMebibytes(profile.importRejectionBytes)}를 넘습니다. ` +
                `데스크톱 앱에서 열어 주세요.`,
        };
    }
    if (bytes > profile.importWarningBytes) {
        return {
            allowed: true,
            warning:
                `${formatMebibytes(bytes)} 문서입니다. 메모리가 부족하면 ` +
                `탭이 닫힐 수 있으니 자주 저장하세요.`,
        };
    }
    return { allowed: true, warning: null };
}
