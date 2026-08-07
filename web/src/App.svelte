<script lang="ts">
    import { onMount } from "svelte";
    import { EngineClient } from "./lib/EngineClient";
    import type { DocumentMeta } from "./lib/EngineClient";

    const engine = new EngineClient();

    let canvas: HTMLCanvasElement;
    let meta = $state<DocumentMeta | null>(null);
    let frameIndex = $state(0);
    let playing = $state(false);
    let status = $state("엔진 로딩 중…");
    let documentName = $state("Wave.ugu");

    let rendering = false;
    let queuedFrame: number | null = null;
    let playTimer: ReturnType<typeof setInterval> | null = null;

    async function drawFrame(frame: number) {
        if (rendering) {
            queuedFrame = frame;
            return;
        }
        rendering = true;
        try {
            const rendered = await engine.renderFrame(frame);
            const context = canvas.getContext("2d");
            if (context) {
                canvas.width = rendered.width;
                canvas.height = rendered.height;
                context.putImageData(
                    new ImageData(
                        rendered.pixels,
                        rendered.width,
                        rendered.height,
                    ),
                    0,
                    0,
                );
            }
        } catch (error) {
            status = `렌더 실패: ${error}`;
        } finally {
            rendering = false;
            if (queuedFrame !== null) {
                const next = queuedFrame;
                queuedFrame = null;
                void drawFrame(next);
            }
        }
    }

    async function openDocument(bytes: ArrayBuffer, name: string) {
        stopPlayback();
        status = `${name} 여는 중…`;
        try {
            meta = await engine.open(bytes);
            documentName = name;
            frameIndex = 0;
            status =
                `${name} — ${meta.width}×${meta.height}, ` +
                `${meta.frameCount}프레임 @ ${meta.fps}fps, ` +
                `레이어 ${meta.layerCount}, 스키마 v${meta.schemaVersion}`;
            await drawFrame(0);
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
            void drawFrame(frameIndex);
        }, 1000 / meta.fps);
    }

    function onSliderInput(event: Event) {
        stopPlayback();
        frameIndex = Number((event.currentTarget as HTMLInputElement).value);
        void drawFrame(frameIndex);
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
        <canvas bind:this={canvas}></canvas>
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
        gap: 1rem;
        padding: 0.6rem 1rem;
        background: #26292f;
    }

    h1 {
        margin: 0;
        font-size: 1.05rem;
        font-weight: 600;
    }

    .controls {
        display: flex;
        gap: 0.5rem;
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

    .file-button input {
        display: none;
    }
</style>
