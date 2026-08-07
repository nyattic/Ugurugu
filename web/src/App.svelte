<script lang="ts">
    import { onMount } from "svelte";
    import { EngineClient } from "./lib/EngineClient";
    import type { DocumentMeta, RenderedFrame } from "./lib/EngineClient";

    const engine = new EngineClient();

    let canvas: HTMLCanvasElement;
    let meta = $state<DocumentMeta | null>(null);
    let frameIndex = $state(0);
    let playing = $state(false);
    let status = $state("엔진 로딩 중…");
    let documentName = $state("Wave.ugu");
    let canUndo = $state(false);
    let canRedo = $state(false);

    let colorHex = $state("#1d2129");
    let brushSize = $state(6);
    let tool = $state<"brush" | "eraser">("brush");

    let playTimer: ReturnType<typeof setInterval> | null = null;
    let drawing = false;
    let pendingPoints: number[] = [];
    let chain = Promise.resolve();

    function enqueue(operation: () => Promise<void>) {
        chain = chain
            .then(operation)
            .catch((error) => {
                status = `오류: ${error}`;
            });
    }

    function presentFrame(rendered: RenderedFrame) {
        const context = canvas.getContext("2d");
        if (!context) {
            return;
        }
        if (
            canvas.width !== rendered.width ||
            canvas.height !== rendered.height
        ) {
            canvas.width = rendered.width;
            canvas.height = rendered.height;
        }
        context.putImageData(
            new ImageData(rendered.pixels, rendered.width, rendered.height),
            0,
            0,
        );
        canUndo = rendered.canUndo;
        canRedo = rendered.canRedo;
    }

    function requestRender(frame: number) {
        enqueue(async () => {
            presentFrame(await engine.renderFrame(frame));
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

    async function openDocument(bytes: ArrayBuffer, name: string) {
        stopPlayback();
        status = `${name} 여는 중…`;
        try {
            meta = await engine.open(bytes);
            documentName = name;
            frameIndex = 0;
            canUndo = meta.canUndo;
            canRedo = meta.canRedo;
            status =
                `${name} — ${meta.width}×${meta.height}, ` +
                `${meta.frameCount}프레임 @ ${meta.fps}fps, ` +
                `레이어 ${meta.layerCount}, 스키마 v${meta.schemaVersion}`;
            requestRender(0);
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
            presentFrame(await engine.strokeAppend(frameIndex, points));
        });
    }

    function onPointerDown(event: PointerEvent) {
        if (!meta || event.button !== 0) {
            return;
        }
        stopPlayback();
        canvas.setPointerCapture(event.pointerId);
        drawing = true;
        pendingPoints = [];
        const { x, y, pressure } = canvasPosition(event);
        const frame = frameIndex;
        enqueue(async () => {
            presentFrame(await engine.strokeBegin(frame, x, y, pressure));
        });
    }

    function onPointerMove(event: PointerEvent) {
        if (!drawing) {
            return;
        }
        const samples =
            "getCoalescedEvents" in event
                ? event.getCoalescedEvents()
                : [event];
        for (const sample of samples) {
            const { x, y, pressure } = canvasPosition(sample);
            pendingPoints.push(x, y, pressure);
        }
        flushPendingPoints();
    }

    function onPointerUp(event: PointerEvent) {
        if (!drawing) {
            return;
        }
        drawing = false;
        canvas.releasePointerCapture(event.pointerId);
        flushPendingPoints();
        const frame = frameIndex;
        enqueue(async () => {
            presentFrame(await engine.strokeEnd(frame));
        });
    }

    function undo() {
        stopPlayback();
        enqueue(async () => {
            presentFrame(await engine.undo(frameIndex));
        });
    }

    function redo() {
        stopPlayback();
        enqueue(async () => {
            presentFrame(await engine.redo(frameIndex));
        });
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

    onMount(() => {
        void (async () => {
            try {
                const response = await fetch("/engine/Wave.ugu");
                await openDocument(await response.arrayBuffer(), "Wave.ugu");
            } catch (error) {
                status = `데모 문서 로드 실패: ${error}`;
            }
        })();
        return stopPlayback;
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
            <input
                type="color"
                bind:value={colorHex}
                title="브러시 색"
                disabled={tool === "eraser"}
            />
            <label class="size">
                굵기
                <input
                    type="range"
                    min="1"
                    max="64"
                    bind:value={brushSize}
                />
                <span>{brushSize}px</span>
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
        </div>
    </header>

    <section class="viewport">
        <canvas
            bind:this={canvas}
            onpointerdown={onPointerDown}
            onpointermove={onPointerMove}
            onpointerup={onPointerUp}
            onpointercancel={onPointerUp}
        ></canvas>
    </section>

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
        gap: 0.5rem;
    }

    .size {
        display: flex;
        align-items: center;
        gap: 0.4rem;
        font-size: 0.85rem;
        color: #9aa0a6;
    }

    .size span {
        min-width: 2.6rem;
        font-variant-numeric: tabular-nums;
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

    button,
    .file-button {
        padding: 0.35rem 0.9rem;
        border: 1px solid #3c4047;
        border-radius: 6px;
        background: #2f333a;
        color: inherit;
        font-size: 0.9rem;
        cursor: pointer;
    }

    button:disabled {
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
