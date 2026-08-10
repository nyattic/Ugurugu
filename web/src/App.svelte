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
    import ToolIcon from "./lib/ToolIcon.svelte";
    import ToolOptions from "./lib/ToolOptions.svelte";
    import ToolRail from "./lib/ToolRail.svelte";
    import { CanvasPresenter } from "./lib/CanvasPresenter";
    import { SelectionOverlay } from "./lib/SelectionOverlay";
    import type { DragShape } from "./lib/SelectionOverlay";
    import { checkImportSize, detectMemoryProfile } from "./lib/MemoryPolicy";
    import type { MemoryProfile } from "./lib/MemoryPolicy";
    import { handleShortcut } from "./lib/Shortcuts";
    import {
        combineForModifiers,
        selectionCombines,
        selectionShapes,
        toolDefinition,
    } from "./lib/tools";
    import type { CombineValue, ToolId } from "./lib/tools";
    import {
        loadToolSettings,
        maximumBrushSize,
        minimumBrushSize,
        saveToolSettings,
    } from "./lib/ToolSettings";
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
    let overlayCanvas: HTMLCanvasElement;
    let viewportElement: HTMLElement;
    let fileInput: HTMLInputElement;
    let presenter: CanvasPresenter | null = null;
    let overlay: SelectionOverlay | null = null;

    let meta = $state<DocumentMeta | null>(null);
    let layers = $state<LayerInfo[]>([]);
    let presets = $state<BrushPresetInfo[]>([]);
    let eraserPresets = $state<BrushPresetInfo[]>([]);
    let frameIndex = $state(0);
    let playing = $state(false);
    let status = $state("Loading engine…");
    let documentName = $state("Untitled.ugu");
    let canUndo = $state(false);
    let canRedo = $state(false);

    let colorHex = $state("#1d2129");
    let tool = $state<ToolId>("brush");
    const settings = $state(loadToolSettings());
    let thumbnails = $state<LayerThumbnail[]>([]);
    let recentColors = $state<string[]>(loadRecentColors());
    let view = $state<ViewState>({ scale: 1, centerX: 0, centerY: 0 });
    let showNewDocument = $state(false);
    let usingWebGL = $state(false);
    let hasSelection = $state(false);
    // Same default as the desktop's canvas/animateWhileDrawing setting.
    let animateWhileDrawing = $state(
        window.localStorage.getItem("ugurugu-web-animate-while-drawing") ===
            "1",
    );

    const recentColorCapacity = 16;
    const zoomPercent = $derived(Math.round(view.scale * 100));
    const activeTool = $derived(toolDefinition(tool));

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
    let selectionDrag: DragShape | null = null;
    let selectionCombine: CombineValue = selectionCombines.replace;
    let antsFrame: number | null = null;
    // Playback renders at most one frame at a time. Without this the timer
    // queued a render every tick regardless of whether the engine had finished
    // the last one, so a document that renders slower than its frame interval
    // built a backlog that outlived the stop button by minutes.
    let playbackRenderPending = false;

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
        // Picking a color says "paint with this", so the eyedropper hands the
        // canvas back to the brush. Every other tool keeps working.
        if (tool === "eyedropper") {
            tool = "brush";
        }
    }

    function describe(error: unknown): string {
        return String(error).replace(/^(Error:\s*)+/, "");
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
            status = describe(error);
        });
    }

    // Same queue, but the caller can wait for its turn to finish. Swapping the
    // document has to run in order with everything else: posted out of band it
    // could land between the halves of an older operation, leaving queued work
    // to be applied to a document the user never chose.
    function enqueueExclusive(operation: () => Promise<void>): Promise<void> {
        const next = chain.then(operation).catch((error) => {
            status = describe(error);
        });
        chain = next;
        return next;
    }

    function present(update: RegionUpdate) {
        layers = update.layers;
        canUndo = update.canUndo;
        canRedo = update.canRedo;
        hasSelection = update.selection.active;
        if (update.selection.contours) {
            overlay?.setContours(update.selection.contours);
            startAnts();
        }
        if (!presenter) {
            return;
        }
        if (update.pixels && update.rect.width > 0) {
            presenter.writeRegion(update.rect, update.pixels);
        }
        presenter.draw(view);
        overlay?.draw(view);
    }

    // The ants only run while there is something to animate, so an idle
    // document costs no frames.
    function startAnts() {
        if (antsFrame !== null || !overlay) {
            return;
        }
        const step = () => {
            if (!overlay?.advance()) {
                antsFrame = null;
                overlay?.draw(view);
                return;
            }
            overlay.draw(view);
            antsFrame = requestAnimationFrame(step);
        };
        antsFrame = requestAnimationFrame(step);
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
        // A viewport with no area is a layout the shell cannot draw into — a
        // hidden tab, a collapsed iframe. Keeping the surfaces at their last
        // real size means the view comes back unchanged instead of clamped to
        // the 1px floor and refitted to nothing.
        if (rect.width <= 0 || rect.height <= 0) {
            return;
        }
        const ratio = window.devicePixelRatio || 1;
        presenter.resizeDisplay(rect.width, rect.height, ratio);
        overlay?.resize(rect.width, rect.height, ratio);
        presenter.draw(view);
        overlay?.draw(view);
    }

    function zoomToFit() {
        const viewport = viewportSize();
        if (!meta || viewport.width <= 0 || viewport.height <= 0) {
            return;
        }
        view = fitToViewport(meta, viewport);
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
        const erase = tool === "eraser";
        const width = erase ? settings.eraserSize : settings.brushSize;
        if (!meta) {
            return;
        }
        enqueue(() =>
            engine.setBrush({ red, green, blue, alpha: 255, width, erase }),
        );
    });

    $effect(() => {
        const index =
            tool === "eraser"
                ? settings.eraserPresetIndex
                : settings.presetIndex;
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
        const strength = settings.stabilization / 100;
        if (!meta) {
            return;
        }
        enqueue(() => engine.setStabilization(strength));
    });

    $effect(() => {
        const antialiasing = settings.brushAntialiasing;
        if (!meta) {
            return;
        }
        enqueue(() => engine.setBrushAntialiasing(antialiasing));
    });

    $effect(() => {
        const options = {
            reference: settings.fillReference,
            comparison: settings.fillComparison,
            tolerance: settings.fillTolerance,
            antialiasing: settings.bucketAntialiasing,
        };
        if (!meta) {
            return;
        }
        enqueue(() => engine.setFillOptions(options));
    });

    $effect(() => {
        saveToolSettings($state.snapshot(settings));
    });

    $effect(() => {
        const current = view;
        presenter?.draw(current);
        overlay?.draw(current);
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

    function adoptDocument(next: DocumentMeta, name: string) {
        meta = next;
        documentName = name;
        frameIndex = 0;
        contentRevision = 0;
        snapshotRevision = 0;
        layers = next.layers;
        presets = next.presets;
        eraserPresets = next.eraserPresets;
        settings.presetIndex = Math.min(
            settings.presetIndex,
            Math.max(0, presets.length - 1),
        );
        settings.eraserPresetIndex = Math.min(
            settings.eraserPresetIndex,
            Math.max(0, eraserPresets.length - 1),
        );
        canUndo = next.canUndo;
        canRedo = next.canRedo;
        hasSelection = false;
        overlay?.setContours([]);
        presenter?.resizeDocument(next.width, next.height);
        resizeDisplay();
        zoomToFit();
        thumbnails = [];
        requestRender(0);
        scheduleThumbnailRefresh();
    }

    function openDocument(bytes: ArrayBuffer, name: string): Promise<void> {
        return enqueueExclusive(async () => {
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
                // The worker keeps the document that was already open when an
                // open fails, so the shell keeps its own state and reports the
                // failure rather than leaving the artist with nothing.
                status = `Open failed: ${describe(error)}`;
            }
        });
    }

    function createDocument(width: number, height: number): Promise<void> {
        showNewDocument = false;
        return enqueueExclusive(async () => {
            stopPlayback();
            status = `Creating a ${width}×${height} document…`;
            try {
                const next = await engine.create(
                    width,
                    height,
                    profile.undoLimit,
                );
                adoptDocument(next, "Untitled.ugu");
                status =
                    `New document — ${next.width}×${next.height}, ` +
                    `${next.frameCount} frames @ ${next.fps} fps, ` +
                    `schema v${next.schemaVersion}`;
            } catch (error) {
                status = `New document failed: ${describe(error)}`;
            }
        });
    }

    function stopPlayback() {
        playing = false;
        playbackRenderPending = false;
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
            // Drop the tick rather than queue behind a render still in flight.
            // Playback shares the request queue with drawing and undo, so a
            // backlog here would stall the whole shell.
            if (playbackRenderPending) {
                return;
            }
            frameIndex = (frameIndex + 1) % meta.frameCount;
            const frame = frameIndex;
            playbackRenderPending = true;
            enqueue(async () => {
                try {
                    present(await engine.renderFrame(frame));
                } finally {
                    playbackRenderPending = false;
                }
            });
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

    function documentPosition(
        event: PointerEvent | { clientX: number; clientY: number },
    ) {
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

    // The batch is taken here, not inside the queued task. Reading the buffer
    // when the task finally ran meant a fast second stroke could reset it
    // first, so the tail of the previous line vanished — or worse, the new
    // stroke's points were already in it and got appended to the line that was
    // still open. Claiming them synchronously ties every batch to the stroke
    // that produced it, because the queue preserves the order they went in.
    function flushPendingPoints() {
        if (pendingPoints.length === 0) {
            return;
        }
        const points = pendingPoints;
        pendingPoints = [];
        enqueue(async () => {
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

    function markContentChanged() {
        contentRevision += 1;
        scheduleThumbnailRefresh();
    }

    function bucketFill(event: PointerEvent) {
        const { x, y } = documentPosition(event);
        const frame = frameIndex;
        const usedColor = colorHex;
        enqueue(async () => {
            present(await engine.bucketFill(frame, x, y));
            recordRecentColor(usedColor);
        });
        markContentChanged();
    }

    function wandSelect(event: PointerEvent) {
        const { x, y } = documentPosition(event);
        const frame = frameIndex;
        const combine = combineForModifiers(event);
        enqueue(async () => {
            present(await engine.selectionFlood(frame, x, y, combine));
        });
    }

    function beginSelectionDrag(event: PointerEvent) {
        const { x, y } = documentPosition(event);
        selectionCombine = combineForModifiers(event);
        selectionDrag = {
            shape: settings.selectionShape,
            points: [x, y],
        };
        overlay?.setDrag(selectionDrag);
        startAnts();
    }

    function continueSelectionDrag(event: PointerEvent) {
        if (!selectionDrag) {
            return;
        }
        const { x, y } = documentPosition(event);
        if (selectionDrag.shape === "freehand") {
            const points = selectionDrag.points;
            const lastX = points[points.length - 2] ?? x;
            const lastY = points[points.length - 1] ?? y;
            // Same 1px gate CanvasWidget::continueAreaSelection applies, so a
            // slow drag does not pile up thousands of coincident vertices.
            if (Math.hypot(x - lastX, y - lastY) < 1) {
                return;
            }
            points.push(x, y);
        } else if (selectionDrag.points.length >= 4) {
            selectionDrag.points[2] = x;
            selectionDrag.points[3] = y;
        } else {
            selectionDrag.points.push(x, y);
        }
        overlay?.setDrag(selectionDrag);
        overlay?.draw(view);
    }

    function endSelectionDrag() {
        const drag = selectionDrag;
        selectionDrag = null;
        overlay?.setDrag(null);
        if (!drag) {
            return;
        }
        const shape = selectionShapes[drag.shape];
        const points = drag.points;
        const combine = selectionCombine;
        const paint = settings.lassoMode === "paint";
        const frame = frameIndex;
        const usedColor = colorHex;
        if (points.length < 4) {
            overlay?.draw(view);
            return;
        }
        enqueue(async () => {
            present(
                await engine.selectionShape(
                    frame,
                    shape,
                    points,
                    combine,
                    paint,
                ),
            );
            if (paint) {
                recordRecentColor(usedColor);
            }
        });
        if (paint) {
            markContentChanged();
        }
    }

    function selectionAction(
        action: "all" | "invert" | "fill" | "delete" | "deselect",
    ) {
        if (!meta) {
            return;
        }
        // Delete and Backspace reach here whether or not anything is selected;
        // asking the engine anyway turned a stray keypress into an error in
        // the status bar.
        if (action !== "all" && action !== "deselect" && !hasSelection) {
            return;
        }
        const frame = frameIndex;
        const usedColor = colorHex;
        enqueue(async () => {
            if (action === "all") {
                present(await engine.selectAll());
                return;
            }
            if (action === "invert") {
                present(await engine.invertSelection());
                return;
            }
            if (action === "deselect") {
                present(await engine.deselect());
                return;
            }
            if (action === "fill") {
                present(await engine.fillSelection(frame));
                recordRecentColor(usedColor);
                return;
            }
            present(await engine.deleteSelection(frame));
        });
        if (action === "fill" || action === "delete") {
            markContentChanged();
        }
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
                // A second finger turns an in-progress stroke into a gesture.
                // The line drawn so far is committed rather than dropped, so
                // starting a pinch never silently throws away a stroke; undo
                // is one keypress away if it was not wanted.
                if (drawing) {
                    drawing = false;
                    pendingPoints = [];
                    enqueue(async () => {
                        present(await engine.strokeEnd(frameIndex));
                        contentRevision += 1;
                    });
                }
                if (selectionDrag) {
                    selectionDrag = null;
                    overlay?.setDrag(null);
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
        if (tool === "bucket") {
            bucketFill(event);
            return;
        }
        if (tool === "wand") {
            wandSelect(event);
            return;
        }
        if (tool === "lasso") {
            beginSelectionDrag(event);
            return;
        }
        drawing = true;
        pendingPoints = [];
        const { x, y, pressure } = canvasPosition(event);
        const frame = frameIndex;
        const timestamp = event.timeStamp;
        enqueue(async () => {
            try {
                present(
                    await engine.strokeBegin(frame, x, y, pressure, timestamp),
                );
            } catch (error) {
                // The engine refused the stroke — a hidden layer, or none to
                // paint on. Drop the local drawing state so the pointer up
                // does not chase a stroke that never started.
                drawing = false;
                pendingPoints = [];
                throw error;
            }
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
        if (
            event.pointerType === "touch" &&
            activeTouches.has(event.pointerId)
        ) {
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
        if (selectionDrag) {
            continueSelectionDrag(event);
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
        if (selectionDrag) {
            displayCanvas.releasePointerCapture(event.pointerId);
            endSelectionDrag();
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

    // A row index is only valid against the layer list the click was made on.
    // Layer commands go through the shared queue, so a second click lands
    // while the first is still in flight and every row after a delete or a
    // move has already been renumbered — two quick presses of Delete used to
    // remove a second, unrelated layer. Resolving the id when the command
    // actually runs is what keeps the engine acting on the layer the artist
    // pointed at, or on nothing at all if it is already gone.
    function layerCommand(
        id: string,
        action: (frame: number, index: number) => Promise<RegionUpdate>,
    ) {
        stopPlayback();
        enqueue(async () => {
            const index = layers.findIndex((layer) => layer.id === id);
            if (index < 0) {
                status = "That layer no longer exists";
                return;
            }
            present(await action(frameIndex, index));
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
            downloadBlob(
                new Blob([bytes], { type: "application/octet-stream" }),
                documentName,
            );
        } catch (error) {
            status = `Save failed: ${describe(error)}`;
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
        // In the document because some browsers ignore click() on a detached
        // anchor, and the URL outlives the call because revoking it in the
        // same turn can cancel a download that has not started reading yet.
        anchor.style.display = "none";
        document.body.append(anchor);
        anchor.click();
        anchor.remove();
        setTimeout(() => URL.revokeObjectURL(url), 60000);
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
            if (
                !target ||
                !["INPUT", "TEXTAREA", "SELECT", "BUTTON"].includes(
                    target.tagName,
                )
            ) {
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
            selectTool: (next) => (tool = next),
            adjustBrushSize: (delta) => {
                const clamp = (value: number) =>
                    Math.min(
                        maximumBrushSize,
                        Math.max(minimumBrushSize, value),
                    );
                if (tool === "eraser") {
                    settings.eraserSize = clamp(settings.eraserSize + delta);
                } else {
                    settings.brushSize = clamp(settings.brushSize + delta);
                }
            },
            zoomBy,
            zoomToFit,
            zoomToActualSize,
            stepFrame,
            togglePlayback,
            selectAll: () => selectionAction("all"),
            invertSelection: () => selectionAction("invert"),
            deselect: () => selectionAction("deselect"),
            fillSelection: () => selectionAction("fill"),
            deleteSelection: () => selectionAction("delete"),
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

    // Switching windows swallows the key up, and a stuck space turns every
    // later click into a pan.
    function onWindowBlur() {
        spaceHeld = false;
    }

    onMount(() => {
        presenter = new CanvasPresenter(
            surfaceCanvas,
            displayCanvas,
            (webgl) => (usingWebGL = webgl),
        );
        overlay = new SelectionOverlay(overlayCanvas);
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
            await createDocument(
                profile.defaultCanvasWidth,
                profile.defaultCanvasHeight,
            );
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
            if (antsFrame !== null) {
                cancelAnimationFrame(antsFrame);
            }
            document.removeEventListener("visibilitychange", onHidden);
            stopPlayback();
        };
    });
</script>

<svelte:window
    onkeydown={onKeyDown}
    onkeyup={onKeyUp}
    onblur={onWindowBlur}
/>

<main>
    <header class="bar">
        <div class="identity">
            <span class="wordmark">Ugurugu</span>
            <span class="document" title={documentName}>{documentName}</span>
        </div>

        <div class="bar-group">
            <button
                id="undo"
                class="icon-button"
                title="Undo (Ctrl/Cmd Z)"
                onclick={undo}
                disabled={!canUndo}
            >
                <ToolIcon name="undo" size={18} />
            </button>
            <button
                id="redo"
                class="icon-button"
                title="Redo (Ctrl/Cmd Shift Z)"
                onclick={redo}
                disabled={!canRedo}
            >
                <ToolIcon name="redo" size={18} />
            </button>
        </div>

        <div class="bar-group">
            <button id="new-document" onclick={() => (showNewDocument = true)}>
                New
            </button>
            <label class="file-button">
                Open
                <input
                    bind:this={fileInput}
                    type="file"
                    accept=".ugu"
                    onchange={onFileChosen}
                />
            </label>
            <button id="save-document" onclick={downloadDocument} disabled={!meta}>
                Save
            </button>
            <button id="export-png" onclick={exportFramePng} disabled={!meta}>
                PNG
            </button>
            <button
                id="export-gif"
                onclick={exportGif}
                disabled={!meta || exportingGif}
            >
                {exportingGif ? "Exporting…" : "GIF"}
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
        <ToolRail
            {tool}
            {hasSelection}
            onselect={(next) => (tool = next)}
            onselectionaction={selectionAction}
        />
        <ToolOptions {tool} {settings} {presets} {eraserPresets} />

        <section class="viewport" bind:this={viewportElement}>
            <canvas
                id="display-canvas"
                bind:this={displayCanvas}
                aria-label="Drawing canvas"
                class:panning={panning || spaceHeld}
                onpointerdown={onPointerDown}
                onpointermove={onPointerMove}
                onpointerup={onPointerUp}
                onpointercancel={onPointerUp}
                onwheel={onWheel}
            ></canvas>
            <canvas
                id="selection-overlay"
                bind:this={overlayCanvas}
                aria-hidden="true"
            ></canvas>
            <div class="zoom-controls">
                <button
                    title="Zoom out (Ctrl/Cmd −)"
                    onclick={() => zoomBy(1 / 1.25)}
                >
                    −
                </button>
                <button
                    id="zoom-fit"
                    title="Fit to window (Ctrl/Cmd 0)"
                    onclick={zoomToFit}
                >
                    {zoomPercent}%
                </button>
                <button
                    title="Zoom in (Ctrl/Cmd +)"
                    onclick={() => zoomBy(1.25)}
                >
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
                onactivate={(id) =>
                    layerCommand(id, (frame, index) =>
                        engine.layerActivate(frame, index),
                    )}
                onvisible={(id, visible) =>
                    layerCommand(id, (frame, index) =>
                        engine.layerVisible(frame, index, visible),
                    )}
                onopacity={(id, opacity) =>
                    layerCommand(id, (frame, index) =>
                        engine.layerOpacity(frame, index, opacity),
                    )}
                onadd={() => layerAction((frame) => engine.layerAdd(frame))}
                onremove={(id) =>
                    layerCommand(id, (frame, index) =>
                        engine.layerRemove(frame, index),
                    )}
                onrename={(id, name) =>
                    layerCommand(id, (frame, index) =>
                        engine.layerRename(frame, index, name),
                    )}
                onmove={(id, offset) =>
                    layerCommand(id, (frame, index) =>
                        engine.layerMove(frame, index, offset),
                    )}
            />
        </div>
    </div>

    <footer>
        <div class="timeline">
            {#if meta}
                <button
                    class="play icon-button"
                    onclick={togglePlayback}
                    disabled={meta.frameCount < 2}
                    title={playing ? "Stop (Enter)" : "Play (Enter)"}
                >
                    <ToolIcon name={playing ? "pause" : "play"} size={18} />
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
                <label
                    class="toggle"
                    title="Keep the wobble running while drawing"
                >
                    <input
                        id="animate-while-drawing"
                        type="checkbox"
                        bind:checked={animateWhileDrawing}
                    />
                    Wobble while drawing
                </label>
            {/if}
        </div>
        <div class="status-bar">
            <p id="status">{status}</p>
            <p id="autosave-status">{autosaveStatus}</p>
            <p id="presenter-status">
                {activeTool.label} · {usingWebGL ? "WebGL 2" : "Canvas 2D"} ·
                {profile.name}
            </p>
        </div>
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
    /*
      Palette and control shapes come from the desktop's Theme.cpp, so the web
      shell reads as the same product rather than a separate tool.
    */
    /*
      The desktop app bundles Pretendard JP, so the shell ships the same family
      cut to Latin and Hangul rather than falling back to whatever system-ui
      resolves to — on Korean Windows that is Malgun Gothic, which sets the same
      words in a visibly different voice. Anyone who already has the font
      installed downloads nothing. BUILDING.md holds the subsetting command.
      The url is relative because itch.io serves from a subdirectory.
    */
    @font-face {
        font-family: "Pretendard JP UI";
        src:
            local("Pretendard JP Medium"),
            local("PretendardJP-Medium"),
            url("./assets/PretendardJP-ui.woff2") format("woff2");
        font-weight: 500 700;
        font-style: normal;
        font-display: swap;
    }

    :global(:root) {
        --ink-950: #141518;
        --ink-900: #1b1d21;
        --ink-850: #202226;
        --ink-800: #24262b;
        --ink-750: #2e3138;
        --line: #3f434b;
        --paper: #e8e8ea;
        --paper-dim: #9aa0a8;
        --paper-faint: #6a6f78;
        --accent: #ffc94a;
        --accent-bed: rgba(255, 201, 74, 0.13);
        --mono: ui-monospace, SFMono-Regular, "SF Mono", Menlo, monospace;
    }

    :global(body) {
        margin: 0;
        background: var(--ink-900);
        color: var(--paper);
        font-family:
            "Pretendard JP UI",
            system-ui,
            -apple-system,
            "Segoe UI",
            sans-serif;
        font-size: 14px;
        -webkit-font-smoothing: antialiased;
    }

    main {
        display: flex;
        flex-direction: column;
        block-size: 100vh;
    }

    .bar {
        display: flex;
        align-items: center;
        gap: 1rem;
        padding: 0.45rem 0.75rem;
        background: var(--ink-900);
        border-block-end: 1px solid var(--line);
    }

    .identity {
        display: flex;
        align-items: baseline;
        gap: 0.6rem;
        min-inline-size: 0;
        margin-inline-end: auto;
    }

    .wordmark {
        font-size: 0.75rem;
        font-weight: 700;
        letter-spacing: 0.22em;
        text-transform: uppercase;
        color: var(--accent);
    }

    .document {
        overflow: hidden;
        font-family: var(--mono);
        font-size: 0.75rem;
        color: var(--paper-dim);
        text-overflow: ellipsis;
        white-space: nowrap;
    }

    .bar-group {
        display: flex;
        align-items: center;
        gap: 0.3rem;
    }

    .workspace {
        flex: 1;
        display: flex;
        min-block-size: 0;
        /* Only reachable between the stacking breakpoint and the width the
           four columns need. Scrolling there beats shrinking the canvas. */
        overflow-x: auto;
    }

    .viewport {
        position: relative;
        flex: 1 1 0;
        /* A floor, not zero. The rail, the option column and the side panels
           are wider than a phone on their own, and the canvas was the only
           column flex could take that difference out of: its basis is zero, so
           it absorbed the whole overflow and came out 0 wide with nothing left
           to draw on. */
        min-inline-size: 12rem;
        overflow: hidden;
        background:
            repeating-conic-gradient(var(--ink-800) 0% 25%, var(--ink-850) 0% 50%)
            50% / 24px 24px;
    }

    .viewport canvas {
        position: absolute;
        inset: 0;
        inline-size: 100%;
        block-size: 100%;
        touch-action: none;
    }

    #display-canvas {
        cursor: crosshair;
    }

    #display-canvas.panning {
        cursor: grab;
    }

    #selection-overlay {
        pointer-events: none;
    }

    .zoom-controls {
        position: absolute;
        inset-block-end: 0.75rem;
        inset-inline-end: 0.75rem;
        display: flex;
        gap: 2px;
        padding: 2px;
        border-radius: 9px;
        background: rgba(20, 21, 24, 0.82);
        backdrop-filter: blur(6px);
    }

    .zoom-controls button {
        min-inline-size: 2.4rem;
        padding: 0.2rem 0.4rem;
        border: 0;
        border-radius: 7px;
        background: transparent;
        font-family: var(--mono);
        font-size: 0.75rem;
        font-variant-numeric: tabular-nums;
    }

    .side {
        display: flex;
        flex: none;
        flex-direction: column;
        inline-size: 15rem;
        min-block-size: 0;
        border-inline-start: 1px solid var(--line);
        background: var(--ink-850);
    }

    /* Below this the four columns cannot stand side by side at any usable
       canvas width, so they stack instead: canvas first at full width, tools
       and panels under it. The full responsive layout is still to come; this
       is the part that has to hold, because a canvas nobody can draw on is a
       broken shell rather than a cramped one. */
    @media (max-width: 48rem) {
        .workspace {
            flex-wrap: wrap;
            overflow-x: hidden;
            overflow-y: auto;
        }

        .viewport {
            order: -1;
            flex: 1 1 100%;
            min-block-size: 55vh;
        }

        .side {
            flex: 1 1 100%;
            inline-size: auto;
            border-inline-start: 0;
            border-block-start: 1px solid var(--line);
        }
    }

    footer {
        border-block-start: 1px solid var(--line);
        background: var(--ink-900);
    }

    .timeline {
        display: flex;
        align-items: center;
        gap: 0.75rem;
        padding: 0.4rem 0.75rem;
        min-block-size: 1.6rem;
    }

    .timeline input[type="range"] {
        flex: 1;
        accent-color: var(--accent);
    }

    .frame-label {
        min-inline-size: 3.5rem;
        font-family: var(--mono);
        font-size: 0.75rem;
        font-variant-numeric: tabular-nums;
        text-align: end;
        color: var(--paper-dim);
    }

    .status-bar {
        display: flex;
        align-items: center;
        gap: 1.2rem;
        padding: 0.3rem 0.75rem 0.45rem;
        border-block-start: 1px solid var(--ink-850);
    }

    .status-bar p {
        margin: 0;
        overflow: hidden;
        font-size: 0.75rem;
        text-overflow: ellipsis;
        white-space: nowrap;
    }

    #status {
        flex: 1;
        color: var(--paper-dim);
    }

    #autosave-status,
    #presenter-status {
        color: var(--paper-faint);
    }

    #presenter-status {
        font-family: var(--mono);
    }

    .toggle {
        display: flex;
        align-items: center;
        gap: 0.35rem;
        font-size: 0.75rem;
        color: var(--paper-dim);
        white-space: nowrap;
        cursor: pointer;
    }

    .toggle input {
        accent-color: var(--accent);
    }

    .recovery-banner {
        display: flex;
        align-items: center;
        gap: 0.75rem;
        padding: 0.5rem 0.75rem;
        background: var(--accent-bed);
        border-block-end: 1px solid var(--line);
        color: var(--accent);
        font-size: 0.8rem;
    }

    button,
    .file-button {
        padding: 0.3rem 0.7rem;
        border: 1px solid var(--line);
        border-radius: 7px;
        background: var(--ink-800);
        color: var(--paper);
        font: inherit;
        font-size: 0.78rem;
        cursor: pointer;
    }

    button:hover:not(:disabled),
    .file-button:hover {
        background: var(--ink-750);
    }

    button:focus-visible,
    .file-button:focus-within {
        outline: 2px solid var(--accent);
        outline-offset: 1px;
    }

    button:disabled {
        opacity: 0.38;
        cursor: default;
    }

    .icon-button {
        display: flex;
        align-items: center;
        justify-content: center;
        inline-size: 2rem;
        block-size: 1.85rem;
        padding: 0;
        color: var(--paper-dim);
    }

    .icon-button:hover:not(:disabled) {
        color: var(--paper);
    }

    .file-button input {
        display: none;
    }
</style>
