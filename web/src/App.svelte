<script lang="ts">
    import { onMount } from "svelte";
    import { EngineClient } from "./lib/EngineClient";
    import type {
        BrushPresetInfo,
        DocumentMeta,
        LayerInfo,
        LayerThumbnail,
        RegionUpdate,
    } from "./lib/EngineClient";
    import ColorPanel from "./lib/ColorPanel.svelte";
    import LayerPanel from "./lib/LayerPanel.svelte";
    import NewDocumentDialog from "./lib/NewDocumentDialog.svelte";
    import { CanvasPresenter } from "./lib/CanvasPresenter";
    import { checkImportSize, detectMemoryProfile } from "./lib/MemoryPolicy";
    import type { MemoryProfile } from "./lib/MemoryPolicy";
    import { handleShortcut } from "./lib/Shortcuts";
    import {
        fitToViewport,
        pan,
        toDocument,
        zoomAround,
    } from "./lib/ViewTransform";
    import type { ViewState } from "./lib/ViewTransform";
    import {
        clearRecoverySnapshot,
        readRecoverySnapshot,
        writeRecoverySnapshot,
    } from "./lib/RecoveryStore";
    import type { RecoverySnapshot } from "./lib/RecoveryStore";

    const engine = new EngineClient();
    const profile: MemoryProfile = detectMemoryProfile();

    let surfaceCanvas: HTMLCanvasElement;
    let displayCanvas: HTMLCanvasElement;
    let viewportElement: HTMLElement;
    let fileInput: HTMLInputElement;
    let presenter: CanvasPresenter | null = null;

    let meta = $state<DocumentMeta | null>(null);
    let layers = $state<LayerInfo[]>([]);
    let presets = $state<BrushPresetInfo[]>([]);
    let eraserPresets = $state<BrushPresetInfo[]>([]);
    let frameIndex = $state(0);
    let playing = $state(false);
    let status = $state("Loading engine…");
    let documentName = $state("Wave.ugu");
    let canUndo = $state(false);
    let canRedo = $state(false);

    let colorHex = $state("#1d2129");
    let brushSize = $state(6);
    let tool = $state<"brush" | "eraser" | "eyedropper">("brush");
    let presetIndex = $state(0);
    let eraserPresetIndex = $state(0);
    let stabilization = $state(0);
    // Matches the desktop brush panel toggle, which also defaults to off.
    let brushAntialiasing = $state(
        window.localStorage.getItem("ugurugu-web-brush-antialiasing") === "1",
    );
    let thumbnails = $state<LayerThumbnail[]>([]);
    let recentColors = $state<string[]>(loadRecentColors());
    let view = $state<ViewState>({ scale: 1, centerX: 0, centerY: 0 });
    let showNewDocument = $state(false);
    let usingWebGL = $state(false);
    // Same default as the desktop's canvas/animateWhileDrawing setting.
    let animateWhileDrawing = $state(
        window.localStorage.getItem("ugurugu-web-animate-while-drawing") ===
            "1",
    );

    const recentColorCapacity = 16;
    const zoomPercent = $derived(Math.round(view.scale * 100));

    let recoveryOffer = $state<RecoverySnapshot | null>(null);
    let autosaveStatus = $state("");
    let exportingGif = $state(false);

    let playTimer: ReturnType<typeof setInterval> | null = null;
    let drawing = false;
    let picking = false;
    let panning = $state(false);
    let spaceHeld = $state(false);
    let lastPanX = 0;
    let lastPanY = 0;
    const activeTouches = new Map<number, { x: number; y: number }>();
    let pinchDistance = 0;
    let pendingPoints: number[] = [];
    let chain = Promise.resolve();
    let contentRevision = 0;
    let snapshotRevision = 0;
    let snapshotBusy = false;
    let thumbnailTimer: ReturnType<typeof setTimeout> | null = null;

    function loadRecentColors(): string[] {
        try {
            const stored = window.localStorage.getItem(
                "ugurugu-web-color-history",
            );
            const parsed = stored ? JSON.parse(stored) : [];
            return Array.isArray(parsed)
                ? parsed.filter(
                      (value) =>
                          typeof value === "string" &&
                          /^#[0-9a-f]{6}$/.test(value),
                  )
                : [];
        } catch {
            return [];
        }
    }

    function recordRecentColor(color: string) {
        recentColors = [
            color,
            ...recentColors.filter((existing) => existing !== color),
        ].slice(0, recentColorCapacity);
        try {
            window.localStorage.setItem(
                "ugurugu-web-color-history",
                JSON.stringify(recentColors),
            );
        } catch {
            // History is a convenience; drawing must not fail on storage.
        }
    }

    function chooseColor(color: string) {
        colorHex = color;
        if (tool !== "brush") {
            tool = "brush";
        }
    }

    function scheduleThumbnailRefresh() {
        if (thumbnailTimer !== null) {
            clearTimeout(thumbnailTimer);
        }
        thumbnailTimer = setTimeout(() => {
            thumbnailTimer = null;
            enqueue(async () => {
                thumbnails = await engine.layerThumbnails(
                    window.devicePixelRatio || 1,
                );
            });
        }, 250);
    }

    function enqueue(operation: () => Promise<void>) {
        chain = chain.then(operation).catch((error) => {
            status = `Error: ${error}`;
        });
    }

    function present(update: RegionUpdate) {
        layers = update.layers;
        canUndo = update.canUndo;
        canRedo = update.canRedo;
        if (!presenter) {
            return;
        }
        if (update.pixels && update.rect.width > 0) {
            presenter.writeRegion(update.rect, update.pixels);
        }
        presenter.draw(view);
    }

    function requestRender(frame: number) {
        enqueue(async () => {
            present(await engine.renderFrame(frame));
        });
    }

    function viewportSize() {
        const rect = displayCanvas.getBoundingClientRect();
        return { width: rect.width, height: rect.height };
    }

    function resizeDisplay() {
        if (!presenter || !viewportElement) {
            return;
        }
        const rect = viewportElement.getBoundingClientRect();
        presenter.resizeDisplay(
            rect.width,
            rect.height,
            window.devicePixelRatio || 1,
        );
        presenter.draw(view);
    }

    function zoomToFit() {
        if (!meta) {
            return;
        }
        view = fitToViewport(meta, viewportSize());
    }

    function zoomToActualSize() {
        if (!meta) {
            return;
        }
        view = { scale: 1, centerX: meta.width / 2, centerY: meta.height / 2 };
    }

    function zoomBy(factor: number) {
        const viewport = viewportSize();
        view = zoomAround(
            view,
            viewport,
            viewport.width / 2,
            viewport.height / 2,
            view.scale * factor,
        );
    }

    $effect(() => {
        const red = Number.parseInt(colorHex.slice(1, 3), 16);
        const green = Number.parseInt(colorHex.slice(3, 5), 16);
        const blue = Number.parseInt(colorHex.slice(5, 7), 16);
        const width = brushSize;
        const erase = tool === "eraser";
        if (!meta) {
            return;
        }
        enqueue(() =>
            engine.setBrush({ red, green, blue, alpha: 255, width, erase }),
        );
    });

    $effect(() => {
        const index = tool === "eraser" ? eraserPresetIndex : presetIndex;
        const erasing = tool === "eraser";
        if (!meta) {
            return;
        }
        enqueue(() =>
            erasing
                ? engine.setEraserPreset(index)
                : engine.setBrushPreset(index),
        );
    });

    $effect(() => {
        const strength = stabilization / 100;
        if (!meta) {
            return;
        }
        enqueue(() => engine.setStabilization(strength));
    });

    $effect(() => {
        const antialiasing = brushAntialiasing;
        try {
            window.localStorage.setItem(
                "ugurugu-web-brush-antialiasing",
                antialiasing ? "1" : "0",
            );
        } catch {
            // A preference that cannot be stored still applies this session.
        }
        if (!meta) {
            return;
        }
        enqueue(() => engine.setBrushAntialiasing(antialiasing));
    });

    $effect(() => {
        const current = view;
        presenter?.draw(current);
    });

    $effect(() => {
        try {
            window.localStorage.setItem(
                "ugurugu-web-animate-while-drawing",
                animateWhileDrawing ? "1" : "0",
            );
        } catch {
            // A preference that cannot be stored still applies this session.
        }
    });

    function onPresetChange(event: Event) {
        const index = Number((event.currentTarget as HTMLSelectElement).value);
        presetIndex = index;
        const preset = presets[index];
        if (preset) {
            brushSize = Math.round(preset.defaultSize);
        }
    }

    function onEraserPresetChange(event: Event) {
        const index = Number((event.currentTarget as HTMLSelectElement).value);
        eraserPresetIndex = index;
        const preset = eraserPresets[index];
        if (preset) {
            brushSize = Math.round(preset.defaultSize);
        }
    }

    function adoptDocument(next: DocumentMeta, name: string) {
        meta = next;
        documentName = name;
        frameIndex = 0;
        contentRevision = 0;
        snapshotRevision = 0;
        layers = next.layers;
        presets = next.presets;
        eraserPresets = next.eraserPresets;
        presetIndex = Math.min(presetIndex, Math.max(0, presets.length - 1));
        eraserPresetIndex = Math.min(
            eraserPresetIndex,
            Math.max(0, eraserPresets.length - 1),
        );
        canUndo = next.canUndo;
        canRedo = next.canRedo;
        presenter?.resizeDocument(next.width, next.height);
        resizeDisplay();
        zoomToFit();
        thumbnails = [];
        requestRender(0);
        scheduleThumbnailRefresh();
    }

    async function openDocument(bytes: ArrayBuffer, name: string) {
        stopPlayback();
        const verdict = checkImportSize(bytes.byteLength, profile);
        if (!verdict.allowed) {
            status = `Cannot open — ${verdict.reason}`;
            return;
        }
        status = `Opening ${name}…`;
        try {
            const next = await engine.open(bytes, profile.undoLimit);
            adoptDocument(next, name);
            const warning = verdict.warning ? ` ⚠ ${verdict.warning}` : "";
            status =
                `${name} — ${next.width}×${next.height}, ` +
                `${next.frameCount} frames @ ${next.fps} fps, ` +
                `schema v${next.schemaVersion}${warning}`;
        } catch (error) {
            meta = null;
            status = `Open failed: ${error}`;
        }
    }

    async function createDocument(width: number, height: number) {
        showNewDocument = false;
        stopPlayback();
        status = `Creating a ${width}×${height} document…`;
        try {
            const next = await engine.create(width, height, profile.undoLimit);
            adoptDocument(next, "Untitled.ugu");
            status =
                `New document — ${width}×${height}, ` +
                `${next.frameCount} frames`;
        } catch (error) {
            status = `New document failed: ${error}`;
        }
    }

    function stopPlayback() {
        playing = false;
        if (playTimer !== null) {
            clearInterval(playTimer);
            playTimer = null;
        }
    }

    function togglePlayback() {
        if (playing) {
            stopPlayback();
            return;
        }
        if (!meta || meta.frameCount < 2) {
            return;
        }
        playing = true;
        playTimer = setInterval(() => {
            // Mirrors CanvasWidget::advanceFrame: playback stays on through an
            // interaction and only stops advancing, so releasing the pointer
            // resumes the wobble instead of leaving it parked.
            if (!meta || panning || picking) {
                return;
            }
            if (drawing && !animateWhileDrawing) {
                return;
            }
            frameIndex = (frameIndex + 1) % meta.frameCount;
            requestRender(frameIndex);
        }, 1000 / meta.fps);
    }

    function stepFrame(delta: number) {
        if (!meta) {
            return;
        }
        stopPlayback();
        const count = meta.frameCount;
        frameIndex = (((frameIndex + delta) % count) + count) % count;
        requestRender(frameIndex);
    }

    function documentPosition(event: PointerEvent | { clientX: number; clientY: number }) {
        const rect = displayCanvas.getBoundingClientRect();
        return toDocument(
            view,
            { width: rect.width, height: rect.height },
            event.clientX - rect.left,
            event.clientY - rect.top,
        );
    }

    function canvasPosition(event: PointerEvent) {
        const { x, y } = documentPosition(event);
        return {
            x,
            y,
            pressure:
                event.pointerType === "mouse"
                    ? 1
                    : event.pressure > 0
                      ? event.pressure
                      : 1,
        };
    }

    function flushPendingPoints() {
        enqueue(async () => {
            if (pendingPoints.length === 0) {
                return;
            }
            const points = pendingPoints;
            pendingPoints = [];
            present(await engine.strokeAppend(frameIndex, points));
        });
    }

    function pickColor(event: PointerEvent) {
        if (!presenter) {
            return;
        }
        const { x, y } = documentPosition(event);
        const [red, green, blue] = presenter.readPixel(x, y);
        colorHex = `#${((red << 16) | (green << 8) | blue)
            .toString(16)
            .padStart(6, "0")}`;
    }

    function beginPan(event: PointerEvent) {
        panning = true;
        lastPanX = event.clientX;
        lastPanY = event.clientY;
        displayCanvas.setPointerCapture(event.pointerId);
    }

    function onPointerDown(event: PointerEvent) {
        if (!meta) {
            return;
        }
        if (event.pointerType === "touch") {
            activeTouches.set(event.pointerId, {
                x: event.clientX,
                y: event.clientY,
            });
            if (activeTouches.size === 2) {
                // A second finger turns an in-progress stroke into a gesture;
                // cancelling beats committing a line the user did not mean.
                if (drawing) {
                    drawing = false;
                    pendingPoints = [];
                    enqueue(async () => {
                        present(await engine.strokeEnd(frameIndex));
                        contentRevision += 1;
                    });
                }
                pinchDistance = touchDistance();
                return;
            }
        }
        if (spaceHeld || event.button === 1) {
            event.preventDefault();
            beginPan(event);
            return;
        }
        if (event.button !== 0) {
            return;
        }
        displayCanvas.setPointerCapture(event.pointerId);
        if (tool === "eyedropper") {
            picking = true;
            pickColor(event);
            return;
        }
        drawing = true;
        pendingPoints = [];
        const { x, y, pressure } = canvasPosition(event);
        const frame = frameIndex;
        const timestamp = event.timeStamp;
        enqueue(async () => {
            present(
                await engine.strokeBegin(frame, x, y, pressure, timestamp),
            );
        });
    }

    function touchPair() {
        const points = [...activeTouches.values()];
        const first = points[0];
        const second = points[1];
        return first && second ? ([first, second] as const) : null;
    }

    function touchDistance() {
        const pair = touchPair();
        if (!pair) {
            return 0;
        }
        return Math.hypot(pair[0].x - pair[1].x, pair[0].y - pair[1].y);
    }

    function touchCenter() {
        const pair = touchPair();
        if (!pair) {
            return { clientX: 0, clientY: 0 };
        }
        return {
            clientX: (pair[0].x + pair[1].x) / 2,
            clientY: (pair[0].y + pair[1].y) / 2,
        };
    }

    function onPointerMove(event: PointerEvent) {
        if (event.pointerType === "touch" && activeTouches.has(event.pointerId)) {
            activeTouches.set(event.pointerId, {
                x: event.clientX,
                y: event.clientY,
            });
            if (activeTouches.size === 2) {
                const distance = touchDistance();
                if (pinchDistance > 0 && distance > 0) {
                    const rect = displayCanvas.getBoundingClientRect();
                    const centre = touchCenter();
                    view = zoomAround(
                        view,
                        { width: rect.width, height: rect.height },
                        centre.clientX - rect.left,
                        centre.clientY - rect.top,
                        view.scale * (distance / pinchDistance),
                    );
                }
                pinchDistance = distance;
                return;
            }
        }
        if (panning) {
            view = pan(
                view,
                event.clientX - lastPanX,
                event.clientY - lastPanY,
            );
            lastPanX = event.clientX;
            lastPanY = event.clientY;
            return;
        }
        if (picking) {
            pickColor(event);
            return;
        }
        if (!drawing) {
            return;
        }
        const samples =
            "getCoalescedEvents" in event
                ? event.getCoalescedEvents()
                : [event];
        for (const sample of samples) {
            const { x, y, pressure } = canvasPosition(sample);
            pendingPoints.push(x, y, pressure, sample.timeStamp);
        }
        flushPendingPoints();
    }

    function onPointerUp(event: PointerEvent) {
        activeTouches.delete(event.pointerId);
        if (activeTouches.size < 2) {
            pinchDistance = 0;
        }
        if (panning) {
            panning = false;
            displayCanvas.releasePointerCapture(event.pointerId);
            return;
        }
        if (picking) {
            picking = false;
            displayCanvas.releasePointerCapture(event.pointerId);
            return;
        }
        if (!drawing) {
            return;
        }
        drawing = false;
        displayCanvas.releasePointerCapture(event.pointerId);
        flushPendingPoints();
        const frame = frameIndex;
        const usedColor = tool === "brush" ? colorHex : null;
        enqueue(async () => {
            present(await engine.strokeEnd(frame));
            contentRevision += 1;
            if (usedColor) {
                recordRecentColor(usedColor);
            }
        });
        scheduleThumbnailRefresh();
    }

    function onWheel(event: WheelEvent) {
        if (!meta) {
            return;
        }
        event.preventDefault();
        const rect = displayCanvas.getBoundingClientRect();
        const viewport = { width: rect.width, height: rect.height };
        if (event.ctrlKey || event.metaKey) {
            view = zoomAround(
                view,
                viewport,
                event.clientX - rect.left,
                event.clientY - rect.top,
                view.scale * Math.exp(-event.deltaY / 320),
            );
            return;
        }
        view = pan(view, -event.deltaX, -event.deltaY);
    }

    function undo() {
        stopPlayback();
        enqueue(async () => {
            present(await engine.undo(frameIndex));
            contentRevision += 1;
        });
        scheduleThumbnailRefresh();
    }

    function redo() {
        stopPlayback();
        enqueue(async () => {
            present(await engine.redo(frameIndex));
            contentRevision += 1;
        });
        scheduleThumbnailRefresh();
    }

    function layerAction(action: (frame: number) => Promise<RegionUpdate>) {
        stopPlayback();
        enqueue(async () => {
            present(await action(frameIndex));
            contentRevision += 1;
        });
        scheduleThumbnailRefresh();
    }

    function onSliderInput(event: Event) {
        stopPlayback();
        frameIndex = Number((event.currentTarget as HTMLInputElement).value);
        requestRender(frameIndex);
    }

    async function onFileChosen(event: Event) {
        const input = event.currentTarget as HTMLInputElement;
        const file = input.files?.[0];
        if (!file) {
            return;
        }
        await openDocument(await file.arrayBuffer(), file.name);
        input.value = "";
    }

    async function downloadDocument() {
        try {
            const bytes = await engine.serialize();
            const url = URL.createObjectURL(
                new Blob([bytes], { type: "application/octet-stream" }),
            );
            const anchor = document.createElement("a");
            anchor.href = url;
            anchor.download = documentName;
            anchor.click();
            URL.revokeObjectURL(url);
        } catch (error) {
            status = `Save failed: ${error}`;
        }
    }

    function exportFramePng() {
        stopPlayback();
        const frame = frameIndex;
        enqueue(async () => {
            present(await engine.renderFrame(frame));
            const blob = await presenter?.toBlob("image/png");
            if (!blob) {
                throw new Error("PNG encoding failed");
            }
            downloadBlob(
                blob,
                `${documentName.replace(/\.ugu$/i, "")}-frame` +
                    `${frame + 1}.png`,
            );
        });
    }

    function downloadBlob(blob: Blob, filename: string) {
        const url = URL.createObjectURL(blob);
        const anchor = document.createElement("a");
        anchor.href = url;
        anchor.download = filename;
        anchor.click();
        URL.revokeObjectURL(url);
    }

    function exportGif() {
        stopPlayback();
        exportingGif = true;
        status = "Exporting GIF…";
        enqueue(async () => {
            try {
                const bytes = await engine.exportGif();
                downloadBlob(
                    new Blob([bytes], { type: "image/gif" }),
                    `${documentName.replace(/\.ugu$/i, "")}.gif`,
                );
                status = "GIF export complete";
            } finally {
                exportingGif = false;
            }
        });
    }

    async function snapshotRecovery() {
        if (
            !meta ||
            drawing ||
            snapshotBusy ||
            contentRevision === snapshotRevision
        ) {
            return;
        }
        snapshotBusy = true;
        const revision = contentRevision;
        try {
            const bytes = await engine.serialize();
            await writeRecoverySnapshot({
                name: documentName,
                bytes,
                savedAt: Date.now(),
            });
            snapshotRevision = revision;
            const time = new Date().toLocaleTimeString();
            autosaveStatus = `Recovery snapshot saved ${time}`;
        } catch (error) {
            const detail =
                error instanceof Error
                    ? `${error.name}: ${error.message}`
                    : String(error);
            autosaveStatus = `Recovery save failed — ${detail}`;
        } finally {
            snapshotBusy = false;
        }
    }

    function restoreRecovery() {
        const offer = recoveryOffer;
        recoveryOffer = null;
        if (!offer) {
            return;
        }
        void openDocument(offer.bytes, offer.name);
    }

    async function discardRecovery() {
        recoveryOffer = null;
        try {
            await clearRecoverySnapshot();
        } catch (error) {
            autosaveStatus = `Could not clear the recovery slot — ${error}`;
        }
    }

    // Reload-safety knob: the interval is short because a browser tab can go
    // away without any reliable shutdown callback. Tests pass ?autosave=1.
    function autosaveIntervalMs() {
        const parameter = new URLSearchParams(window.location.search).get(
            "autosave",
        );
        const seconds = Number(parameter);
        if (!Number.isFinite(seconds) || seconds <= 0) {
            return 15000;
        }
        return Math.min(600, Math.max(1, seconds)) * 1000;
    }

    function onKeyDown(event: KeyboardEvent) {
        if (event.key === " " && !spaceHeld) {
            const target = event.target as HTMLElement | null;
            if (!target || !["INPUT", "TEXTAREA", "SELECT", "BUTTON"].includes(target.tagName)) {
                spaceHeld = true;
                event.preventDefault();
            }
            return;
        }
        if (showNewDocument) {
            return;
        }
        const handled = handleShortcut(event, {
            undo,
            redo,
            save: () => void downloadDocument(),
            open: () => fileInput?.click(),
            newDocument: () => (showNewDocument = true),
            selectBrush: () => (tool = "brush"),
            selectEraser: () => (tool = "eraser"),
            selectEyedropper: () => (tool = "eyedropper"),
            adjustBrushSize: (delta) => {
                brushSize = Math.min(64, Math.max(1, brushSize + delta));
            },
            zoomBy,
            zoomToFit,
            zoomToActualSize,
            stepFrame,
            togglePlayback,
        });
        if (handled) {
            event.preventDefault();
        }
    }

    function onKeyUp(event: KeyboardEvent) {
        if (event.key === " ") {
            spaceHeld = false;
        }
    }

    onMount(() => {
        presenter = new CanvasPresenter(surfaceCanvas, displayCanvas);
        usingWebGL = presenter.usingWebGL;
        const observer = new ResizeObserver(() => resizeDisplay());
        observer.observe(viewportElement);
        resizeDisplay();
        void (async () => {
            try {
                recoveryOffer = await readRecoverySnapshot();
            } catch (error) {
                autosaveStatus = `Could not read the recovery slot — ${error}`;
            }
            try {
                const response = await fetch(
                    new URL("engine/Wave.ugu", document.baseURI),
                );
                await openDocument(await response.arrayBuffer(), "Wave.ugu");
            } catch (error) {
                status = `Demo document failed to load: ${error}`;
            }
        })();
        const snapshotTimer = setInterval(() => {
            void snapshotRecovery();
        }, autosaveIntervalMs());
        const onHidden = () => {
            if (document.visibilityState === "hidden") {
                void snapshotRecovery();
            }
        };
        document.addEventListener("visibilitychange", onHidden);
        return () => {
            observer.disconnect();
            clearInterval(snapshotTimer);
            if (thumbnailTimer !== null) {
                clearTimeout(thumbnailTimer);
            }
            document.removeEventListener("visibilitychange", onHidden);
            stopPlayback();
        };
    });
</script>

<svelte:window onkeydown={onKeyDown} onkeyup={onKeyUp} />

<main>
    <header>
        <h1>Ugurugu Web</h1>
        <div class="tools">
            <button
                class="tool-button"
                class:active={tool === "brush"}
                title="Brush (B)"
                onclick={() => (tool = "brush")}
            >
                Brush
            </button>
            <button
                class="tool-button"
                class:active={tool === "eraser"}
                title="Eraser (E)"
                onclick={() => (tool = "eraser")}
            >
                Eraser
            </button>
            <button
                id="eyedropper"
                class="tool-button"
                class:active={tool === "eyedropper"}
                title="Pick a color from the canvas (I)"
                onclick={() => (tool = "eyedropper")}
            >
                Eyedropper
            </button>
            {#if tool === "eraser"}
                <select
                    id="eraser-preset"
                    title="Eraser preset"
                    value={String(eraserPresetIndex)}
                    onchange={onEraserPresetChange}
                >
                    {#each eraserPresets as preset (preset.index)}
                        <option value={String(preset.index)}>
                            {preset.name}
                        </option>
                    {/each}
                </select>
            {:else}
                <select
                    title="Brush preset"
                    value={String(presetIndex)}
                    onchange={onPresetChange}
                >
                    {#each presets as preset (preset.index)}
                        <option value={String(preset.index)}>
                            {preset.name}
                        </option>
                    {/each}
                </select>
                <input
                    type="color"
                    bind:value={colorHex}
                    title="Brush color"
                />
            {/if}
            <label class="slider">
                Size
                <input
                    type="range"
                    min="1"
                    max="64"
                    bind:value={brushSize}
                />
                <span>{brushSize}px</span>
            </label>
            <label class="toggle" title="Antialias brush edges">
                <input
                    id="brush-antialiasing"
                    type="checkbox"
                    bind:checked={brushAntialiasing}
                />
                Antialias
            </label>
            <label class="slider">
                Smoothing
                <input
                    type="range"
                    min="0"
                    max="100"
                    bind:value={stabilization}
                />
                <span>{stabilization}%</span>
            </label>
            <button id="undo" onclick={undo} disabled={!canUndo}>
                Undo
            </button>
            <button id="redo" onclick={redo} disabled={!canRedo}>
                Redo
            </button>
        </div>
        <div class="controls">
            <button id="new-document" onclick={() => (showNewDocument = true)}>
                New
            </button>
            <label class="file-button">
                Open .ugu
                <input
                    bind:this={fileInput}
                    type="file"
                    accept=".ugu"
                    onchange={onFileChosen}
                />
            </label>
            <button onclick={downloadDocument} disabled={!meta}>
                Save .ugu
            </button>
            <button id="export-png" onclick={exportFramePng} disabled={!meta}>
                Export PNG
            </button>
            <button
                id="export-gif"
                onclick={exportGif}
                disabled={!meta || exportingGif}
            >
                {exportingGif ? "Exporting GIF…" : "Export GIF"}
            </button>
        </div>
    </header>

    {#if recoveryOffer}
        <div class="recovery-banner" role="alert">
            <span>
                Unsaved work from an earlier session —
                {recoveryOffer.name},
                {new Date(recoveryOffer.savedAt).toLocaleString()}
            </span>
            <button id="recovery-restore" onclick={restoreRecovery}>
                Restore
            </button>
            <button id="recovery-discard" onclick={discardRecovery}>
                Discard
            </button>
        </div>
    {/if}

    <!--
      Document-resolution surface. Hidden from layout but still the authority
      for pixels: the eyedropper samples it and PNG export encodes it, so both
      stay independent of the current zoom.
    -->
    <canvas id="document-surface" bind:this={surfaceCanvas} hidden></canvas>

    <div class="workspace">
        <section class="viewport" bind:this={viewportElement}>
            <canvas
                bind:this={displayCanvas}
                aria-label="Drawing canvas"
                class:panning={panning || spaceHeld}
                onpointerdown={onPointerDown}
                onpointermove={onPointerMove}
                onpointerup={onPointerUp}
                onpointercancel={onPointerUp}
                onwheel={onWheel}
            ></canvas>
            <div class="zoom-controls">
                <button
                    title="Zoom out (Ctrl/Cmd −)"
                    onclick={() => zoomBy(1 / 1.25)}
                >
                    −
                </button>
                <button id="zoom-fit" title="Fit to window (Ctrl/Cmd 0)" onclick={zoomToFit}>
                    {zoomPercent}%
                </button>
                <button title="Zoom in (Ctrl/Cmd +)" onclick={() => zoomBy(1.25)}>
                    +
                </button>
            </div>
        </section>
        <div class="side">
            <ColorPanel
                color={colorHex}
                {recentColors}
                onchange={(hex) => (colorHex = hex)}
                onrecent={chooseColor}
            />
            <LayerPanel
                {layers}
                {thumbnails}
                onactivate={(index) =>
                    layerAction((frame) => engine.layerActivate(frame, index))}
                onvisible={(index, visible) =>
                    layerAction((frame) =>
                        engine.layerVisible(frame, index, visible),
                    )}
                onopacity={(index, opacity) =>
                    layerAction((frame) =>
                        engine.layerOpacity(frame, index, opacity),
                    )}
                onadd={() => layerAction((frame) => engine.layerAdd(frame))}
                onremove={(index) =>
                    layerAction((frame) => engine.layerRemove(frame, index))}
                onrename={(index, name) =>
                    layerAction((frame) =>
                        engine.layerRename(frame, index, name),
                    )}
                onmove={(index, offset) =>
                    layerAction((frame) =>
                        engine.layerMove(frame, index, offset),
                    )}
            />
        </div>
    </div>

    <footer>
        {#if meta}
            <button
                class="play"
                onclick={togglePlayback}
                disabled={meta.frameCount < 2}
            >
                {playing ? "Stop" : "Play"}
            </button>
            <input
                type="range"
                min="0"
                max={meta.frameCount - 1}
                value={frameIndex}
                aria-label="Frame"
                oninput={onSliderInput}
            />
            <span class="frame-label">
                {frameIndex + 1}/{meta.frameCount}
            </span>
            <label class="toggle" title="Keep the wobble running while drawing">
                <input
                    id="animate-while-drawing"
                    type="checkbox"
                    bind:checked={animateWhileDrawing}
                />
                Wobble while drawing
            </label>
        {/if}
        <p id="status">{status}</p>
        <p id="autosave-status">{autosaveStatus}</p>
        <p id="presenter-status">
            {usingWebGL ? "WebGL 2" : "Canvas 2D"} · {profile.name}
        </p>
    </footer>
</main>

{#if showNewDocument}
    <NewDocumentDialog
        {profile}
        oncreate={(width, height) => void createDocument(width, height)}
        oncancel={() => (showNewDocument = false)}
    />
{/if}

<style>
    :global(body) {
        margin: 0;
        background: #1d1f24;
        color: #e6e6e6;
        font-family: system-ui, sans-serif;
    }

    main {
        display: flex;
        flex-direction: column;
        block-size: 100vh;
    }

    header {
        display: flex;
        align-items: center;
        justify-content: space-between;
        flex-wrap: wrap;
        gap: 0.75rem;
        padding: 0.6rem 1rem;
        background: #26292f;
    }

    h1 {
        margin: 0;
        font-size: 1.05rem;
        font-weight: 600;
    }

    .tools,
    .controls {
        display: flex;
        align-items: center;
        flex-wrap: wrap;
        gap: 0.5rem;
    }

    .slider {
        display: flex;
        align-items: center;
        gap: 0.4rem;
        font-size: 0.85rem;
        color: #9aa0a6;
    }

    .slider input[type="range"] {
        inline-size: 6rem;
    }

    .slider span {
        min-width: 2.6rem;
        font-variant-numeric: tabular-nums;
    }

    .workspace {
        flex: 1;
        display: flex;
        min-block-size: 0;
    }

    .viewport {
        position: relative;
        flex: 1;
        min-inline-size: 0;
        overflow: hidden;
        background:
            repeating-conic-gradient(#232529 0% 25%, #1d1f24 0% 50%) 50% / 24px
            24px;
    }

    .viewport canvas {
        position: absolute;
        inset: 0;
        inline-size: 100%;
        block-size: 100%;
        touch-action: none;
        cursor: crosshair;
    }

    .viewport canvas.panning {
        cursor: grab;
    }

    .zoom-controls {
        position: absolute;
        inset-block-end: 0.75rem;
        inset-inline-end: 0.75rem;
        display: flex;
        gap: 0.25rem;
    }

    .zoom-controls button {
        min-inline-size: 2.5rem;
        padding: 0.25rem 0.5rem;
        font-size: 0.8rem;
    }

    .side {
        display: flex;
        flex-direction: column;
        inline-size: 15rem;
        min-block-size: 0;
        border-inline-start: 1px solid #3c4047;
        background: #26292f;
    }

    footer {
        display: flex;
        align-items: center;
        gap: 0.75rem;
        padding: 0.6rem 1rem;
        background: #26292f;
    }

    footer input[type="range"] {
        flex: 1;
    }

    .frame-label {
        min-width: 4rem;
        text-align: right;
        font-variant-numeric: tabular-nums;
    }

    .toggle {
        display: flex;
        align-items: center;
        gap: 0.3rem;
        font-size: 0.85rem;
        color: #9aa0a6;
        white-space: nowrap;
    }

    #status {
        margin: 0;
        font-size: 0.85rem;
        color: #9aa0a6;
        white-space: nowrap;
    }

    #autosave-status,
    #presenter-status {
        margin: 0;
        font-size: 0.85rem;
        color: #7f8b99;
        white-space: nowrap;
    }

    .recovery-banner {
        display: flex;
        align-items: center;
        gap: 0.75rem;
        padding: 0.5rem 1rem;
        background: #4a3b1f;
        color: #f4e3bd;
        font-size: 0.9rem;
    }

    button,
    .file-button,
    select {
        padding: 0.35rem 0.9rem;
        border: 1px solid #3c4047;
        border-radius: 6px;
        background: #2f333a;
        color: inherit;
        font-size: 0.9rem;
        cursor: pointer;
    }

    button:disabled,
    select:disabled {
        opacity: 0.45;
        cursor: default;
    }

    .tool-button.active {
        border-color: #4f8ef7;
        background: #34415a;
    }

    input[type="color"] {
        inline-size: 2.2rem;
        block-size: 2rem;
        padding: 0.1rem;
        border: 1px solid #3c4047;
        border-radius: 6px;
        background: #2f333a;
    }

    .file-button input {
        display: none;
    }
</style>
