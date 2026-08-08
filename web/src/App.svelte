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
    import {
        clearRecoverySnapshot,
        readRecoverySnapshot,
        writeRecoverySnapshot,
    } from "./lib/RecoveryStore";
    import type { RecoverySnapshot } from "./lib/RecoveryStore";

    const engine = new EngineClient();

    let canvas: HTMLCanvasElement;
    let meta = $state<DocumentMeta | null>(null);
    let layers = $state<LayerInfo[]>([]);
    let presets = $state<BrushPresetInfo[]>([]);
    let frameIndex = $state(0);
    let playing = $state(false);
    let status = $state("엔진 로딩 중…");
    let documentName = $state("Wave.ugu");
    let canUndo = $state(false);
    let canRedo = $state(false);

    let colorHex = $state("#1d2129");
    let brushSize = $state(6);
    let tool = $state<"brush" | "eraser" | "eyedropper">("brush");
    let presetIndex = $state(0);
    let stabilization = $state(0);
    let thumbnails = $state<LayerThumbnail[]>([]);
    let recentColors = $state<string[]>(loadRecentColors());

    const recentColorCapacity = 16;

    let recoveryOffer = $state<RecoverySnapshot | null>(null);
    let autosaveStatus = $state("");
    let exportingGif = $state(false);

    let playTimer: ReturnType<typeof setInterval> | null = null;
    let drawing = false;
    let picking = false;
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
            status = `오류: ${error}`;
        });
    }

    function present(update: RegionUpdate) {
        layers = update.layers;
        canUndo = update.canUndo;
        canRedo = update.canRedo;
        if (!update.pixels || update.rect.width <= 0) {
            return;
        }
        const context = canvas.getContext("2d");
        if (!context) {
            return;
        }
        context.putImageData(
            new ImageData(
                update.pixels,
                update.rect.width,
                update.rect.height,
            ),
            update.rect.x,
            update.rect.y,
        );
    }

    function requestRender(frame: number) {
        enqueue(async () => {
            present(await engine.renderFrame(frame));
        });
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
        const index = presetIndex;
        if (!meta) {
            return;
        }
        enqueue(() => engine.setBrushPreset(index));
    });

    $effect(() => {
        const strength = stabilization / 100;
        if (!meta) {
            return;
        }
        enqueue(() => engine.setStabilization(strength));
    });

    function onPresetChange(event: Event) {
        const index = Number(
            (event.currentTarget as HTMLSelectElement).value,
        );
        presetIndex = index;
        const preset = presets[index];
        if (preset) {
            brushSize = Math.round(preset.defaultSize);
        }
    }

    async function openDocument(bytes: ArrayBuffer, name: string) {
        stopPlayback();
        status = `${name} 여는 중…`;
        try {
            meta = await engine.open(bytes);
            documentName = name;
            frameIndex = 0;
            contentRevision = 0;
            snapshotRevision = 0;
            layers = meta.layers;
            presets = meta.presets;
            canUndo = meta.canUndo;
            canRedo = meta.canRedo;
            canvas.width = meta.width;
            canvas.height = meta.height;
            status =
                `${name} — ${meta.width}×${meta.height}, ` +
                `${meta.frameCount}프레임 @ ${meta.fps}fps, ` +
                `스키마 v${meta.schemaVersion}`;
            requestRender(0);
            thumbnails = [];
            scheduleThumbnailRefresh();
        } catch (error) {
            meta = null;
            status = `열기 실패: ${error}`;
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
            if (!meta) {
                return;
            }
            frameIndex = (frameIndex + 1) % meta.frameCount;
            requestRender(frameIndex);
        }, 1000 / meta.fps);
    }

    function canvasPosition(event: PointerEvent) {
        const rect = canvas.getBoundingClientRect();
        return {
            x: ((event.clientX - rect.left) * canvas.width) / rect.width,
            y: ((event.clientY - rect.top) * canvas.height) / rect.height,
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
        const context = canvas.getContext("2d");
        if (!context) {
            return;
        }
        const { x, y } = canvasPosition(event);
        const pixelX = Math.min(canvas.width - 1, Math.max(0, Math.floor(x)));
        const pixelY = Math.min(canvas.height - 1, Math.max(0, Math.floor(y)));
        const [red = 0, green = 0, blue = 0] = context.getImageData(
            pixelX,
            pixelY,
            1,
            1,
        ).data;
        colorHex = `#${((red << 16) | (green << 8) | blue)
            .toString(16)
            .padStart(6, "0")}`;
    }

    function onPointerDown(event: PointerEvent) {
        if (!meta || event.button !== 0) {
            return;
        }
        stopPlayback();
        canvas.setPointerCapture(event.pointerId);
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

    function onPointerMove(event: PointerEvent) {
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
        if (picking) {
            picking = false;
            canvas.releasePointerCapture(event.pointerId);
            return;
        }
        if (!drawing) {
            return;
        }
        drawing = false;
        canvas.releasePointerCapture(event.pointerId);
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

    function layerAction(
        action: (frame: number) => Promise<RegionUpdate>,
    ) {
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
            status = `저장 실패: ${error}`;
        }
    }

    function exportFramePng() {
        stopPlayback();
        const frame = frameIndex;
        enqueue(async () => {
            present(await engine.renderFrame(frame));
            const blob = await new Promise<Blob | null>((resolve) => {
                canvas.toBlob(resolve, "image/png");
            });
            if (!blob) {
                throw new Error("PNG 인코딩에 실패했습니다");
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
        status = "GIF 내보내는 중…";
        enqueue(async () => {
            try {
                const bytes = await engine.exportGif();
                downloadBlob(
                    new Blob([bytes], { type: "image/gif" }),
                    `${documentName.replace(/\.ugu$/i, "")}.gif`,
                );
                status = "GIF 내보내기 완료";
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
            autosaveStatus = `복구 스냅샷 저장됨 ${time}`;
        } catch (error) {
            const detail =
                error instanceof Error
                    ? `${error.name}: ${error.message}`
                    : String(error);
            autosaveStatus = `복구 저장 실패 — ${detail}`;
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
            autosaveStatus = `복구 슬롯 삭제 실패 — ${error}`;
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

    onMount(() => {
        void (async () => {
            try {
                recoveryOffer = await readRecoverySnapshot();
            } catch (error) {
                autosaveStatus = `복구 슬롯 확인 실패 — ${error}`;
            }
            try {
                const response = await fetch("/engine/Wave.ugu");
                await openDocument(await response.arrayBuffer(), "Wave.ugu");
            } catch (error) {
                status = `데모 문서 로드 실패: ${error}`;
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
            clearInterval(snapshotTimer);
            if (thumbnailTimer !== null) {
                clearTimeout(thumbnailTimer);
            }
            document.removeEventListener("visibilitychange", onHidden);
            stopPlayback();
        };
    });
</script>

<main>
    <header>
        <h1>Ugurugu Web</h1>
        <div class="tools">
            <button
                class="tool-button"
                class:active={tool === "brush"}
                onclick={() => (tool = "brush")}
            >
                브러시
            </button>
            <button
                class="tool-button"
                class:active={tool === "eraser"}
                onclick={() => (tool = "eraser")}
            >
                지우개
            </button>
            <button
                id="eyedropper"
                class="tool-button"
                class:active={tool === "eyedropper"}
                title="캔버스에서 색 추출"
                onclick={() => (tool = "eyedropper")}
            >
                스포이드
            </button>
            <select
                title="브러시 프리셋"
                value={String(presetIndex)}
                onchange={onPresetChange}
                disabled={tool === "eraser"}
            >
                {#each presets as preset (preset.index)}
                    <option value={String(preset.index)}>{preset.name}</option>
                {/each}
            </select>
            <input
                type="color"
                bind:value={colorHex}
                title="브러시 색"
                disabled={tool === "eraser"}
            />
            <label class="slider">
                굵기
                <input
                    type="range"
                    min="1"
                    max="64"
                    bind:value={brushSize}
                />
                <span>{brushSize}px</span>
            </label>
            <label class="slider">
                보정
                <input
                    type="range"
                    min="0"
                    max="100"
                    bind:value={stabilization}
                />
                <span>{stabilization}%</span>
            </label>
            <button id="undo" onclick={undo} disabled={!canUndo}>
                실행 취소
            </button>
            <button id="redo" onclick={redo} disabled={!canRedo}>
                다시 실행
            </button>
        </div>
        <div class="controls">
            <label class="file-button">
                .ugu 열기
                <input
                    type="file"
                    accept=".ugu"
                    onchange={onFileChosen}
                />
            </label>
            <button onclick={downloadDocument} disabled={!meta}>
                .ugu 저장
            </button>
            <button id="export-png" onclick={exportFramePng} disabled={!meta}>
                PNG 내보내기
            </button>
            <button
                id="export-gif"
                onclick={exportGif}
                disabled={!meta || exportingGif}
            >
                {exportingGif ? "GIF 내보내는 중…" : "GIF 내보내기"}
            </button>
        </div>
    </header>

    {#if recoveryOffer}
        <div class="recovery-banner" role="alert">
            <span>
                저장되지 않은 작업이 있습니다 —
                {recoveryOffer.name},
                {new Date(recoveryOffer.savedAt).toLocaleString()}
            </span>
            <button id="recovery-restore" onclick={restoreRecovery}>
                복구
            </button>
            <button id="recovery-discard" onclick={discardRecovery}>
                삭제
            </button>
        </div>
    {/if}

    <div class="workspace">
        <section class="viewport">
            <canvas
                bind:this={canvas}
                onpointerdown={onPointerDown}
                onpointermove={onPointerMove}
                onpointerup={onPointerUp}
                onpointercancel={onPointerUp}
            ></canvas>
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
                    layerAction((frame) =>
                        engine.layerActivate(frame, index),
                    )}
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
                {playing ? "정지" : "재생"}
            </button>
            <input
                type="range"
                min="0"
                max={meta.frameCount - 1}
                value={frameIndex}
                oninput={onSliderInput}
            />
            <span class="frame-label">
                {frameIndex + 1}/{meta.frameCount}
            </span>
        {/if}
        <p id="status">{status}</p>
        <p id="autosave-status">{autosaveStatus}</p>
    </footer>
</main>

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
        min-height: 100vh;
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
        flex: 1;
        display: grid;
        place-items: center;
        padding: 1rem;
        overflow: auto;
    }

    canvas {
        max-width: 100%;
        max-height: 70vh;
        background: #fff;
        box-shadow: 0 4px 24px rgb(0 0 0 / 45%);
        touch-action: none;
        cursor: crosshair;
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

    #status {
        margin: 0;
        font-size: 0.85rem;
        color: #9aa0a6;
        white-space: nowrap;
    }

    #autosave-status {
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
