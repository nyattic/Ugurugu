<script lang="ts">
    import { untrack } from "svelte";
    import type { DocumentMeta } from "./EngineClient";
    import { clampCanvasEdge, type MemoryProfile } from "./MemoryPolicy";

    let {
        meta,
        profile,
        onimage,
        oncanvas,
        oncancel,
    }: {
        meta: DocumentMeta;
        profile: MemoryProfile;
        onimage: (width: number, height: number) => void;
        // The offset places the existing artwork inside the new canvas, the
        // way CanvasSizeDialog's anchor grid does.
        oncanvas: (
            width: number,
            height: number,
            offsetX: number,
            offsetY: number,
        ) => void;
        oncancel: () => void;
    } = $props();

    let scaleArtwork = $state(true);
    let width = $state(untrack(() => meta.width));
    let height = $state(untrack(() => meta.height));
    let anchor = $state(4);

    const nextWidth = $derived(clampCanvasEdge(width, profile));
    const nextHeight = $derived(clampCanvasEdge(height, profile));

    function alignedOffset(delta: number, alignment: number) {
        if (alignment === 1) {
            return Math.trunc(delta / 2);
        }
        return alignment === 2 ? delta : 0;
    }

    const offsetX = $derived(
        alignedOffset(nextWidth - meta.width, anchor % 3),
    );
    const offsetY = $derived(
        alignedOffset(nextHeight - meta.height, Math.floor(anchor / 3)),
    );
    const clips = $derived(
        !scaleArtwork &&
            (nextWidth < meta.width || nextHeight < meta.height),
    );

    function confirm() {
        if (scaleArtwork) {
            onimage(nextWidth, nextHeight);
            return;
        }
        oncanvas(nextWidth, nextHeight, offsetX, offsetY);
    }
</script>

<div
    class="scrim"
    role="dialog"
    aria-modal="true"
    aria-label="Document size"
    tabindex="-1"
    onkeydown={(event) => {
        if (event.key === "Escape") {
            oncancel();
        }
    }}
>
    <div class="dialog">
        <h2>Document size</h2>

        <div class="segment" role="group" aria-label="Resize mode">
            <button
                id="resize-mode-image"
                class:on={scaleArtwork}
                aria-pressed={scaleArtwork}
                onclick={() => (scaleArtwork = true)}
            >
                Image size
            </button>
            <button
                id="resize-mode-canvas"
                class:on={!scaleArtwork}
                aria-pressed={!scaleArtwork}
                onclick={() => (scaleArtwork = false)}
            >
                Canvas size
            </button>
        </div>

        <p class="note">
            {scaleArtwork
                ? "Scales the artwork with the canvas."
                : "Changes the canvas bounds and leaves the artwork at its size."}
        </p>

        <div class="row">
            <label>
                <span>Width</span>
                <input
                    id="resize-width"
                    type="number"
                    min="1"
                    max={profile.maximumCanvasEdge}
                    bind:value={width}
                />
            </label>
            <label>
                <span>Height</span>
                <input
                    id="resize-height"
                    type="number"
                    min="1"
                    max={profile.maximumCanvasEdge}
                    bind:value={height}
                />
            </label>
        </div>

        {#if !scaleArtwork}
            <div class="field">
                <span class="field-label">Artwork anchor</span>
                <div class="anchors" role="group" aria-label="Artwork anchor">
                    {#each [0, 1, 2, 3, 4, 5, 6, 7, 8] as cell (cell)}
                        <button
                            id={`resize-anchor-${cell}`}
                            class:on={anchor === cell}
                            aria-pressed={anchor === cell}
                            aria-label={`Anchor ${cell}`}
                            onclick={() => (anchor = cell)}
                        ></button>
                    {/each}
                </div>
                <span class="offset">
                    Artwork offset: X {offsetX}, Y {offsetY}
                </span>
            </div>
        {/if}

        <p class="summary">
            {meta.width} × {meta.height} px → {nextWidth} × {nextHeight} px
        </p>
        {#if clips}
            <p class="warn">Artwork outside the new canvas will be clipped.</p>
        {/if}

        <div class="actions">
            <button onclick={oncancel}>Cancel</button>
            <button
                id="resize-confirm"
                class="primary"
                disabled={nextWidth === meta.width &&
                    nextHeight === meta.height}
                onclick={confirm}
            >
                Apply
            </button>
        </div>
    </div>
</div>

<style>
    .scrim {
        position: fixed;
        inset: 0;
        display: grid;
        place-items: center;
        background: rgba(0, 0, 0, 0.55);
        z-index: 20;
    }

    .dialog {
        display: flex;
        flex-direction: column;
        gap: 0.6rem;
        min-inline-size: 19rem;
        padding: 1rem;
        border: 1px solid var(--line);
        border-radius: 10px;
        background: var(--ink-850);
    }

    h2 {
        margin: 0;
        font-size: 0.625rem;
        font-weight: 700;
        letter-spacing: 0.13em;
        text-transform: uppercase;
        color: var(--paper-dim);
    }

    .segment {
        display: flex;
        gap: 0.15rem;
    }

    .segment button {
        flex: 1;
        padding: 0.3rem 0;
        border: 1px solid var(--line);
        border-radius: 6px;
        background: var(--ink-800);
        color: var(--paper-dim);
        font-size: 0.75rem;
        cursor: pointer;
    }

    .segment button.on {
        border-color: var(--accent);
        background: var(--accent-bed);
        color: var(--accent);
    }

    .note,
    .summary,
    .warn,
    .offset {
        margin: 0;
        font-size: 0.75rem;
        color: var(--paper-dim);
    }

    .warn {
        color: var(--accent);
    }

    .row {
        display: flex;
        gap: 0.6rem;
    }

    .row label {
        display: flex;
        flex: 1;
        flex-direction: column;
        gap: 0.25rem;
        font-size: 0.625rem;
        font-weight: 700;
        letter-spacing: 0.1em;
        text-transform: uppercase;
        color: var(--paper-dim);
    }

    .row input {
        inline-size: 100%;
        padding: 0.25rem 0.4rem;
        border: 1px solid var(--line);
        border-radius: 6px;
        background: var(--ink-800);
        color: var(--paper);
        font-family: var(--mono);
        font-variant-numeric: tabular-nums;
    }

    .field {
        display: flex;
        flex-direction: column;
        gap: 0.3rem;
    }

    .field-label {
        font-size: 0.625rem;
        font-weight: 700;
        letter-spacing: 0.1em;
        text-transform: uppercase;
        color: var(--paper-dim);
    }

    .anchors {
        display: grid;
        grid-template-columns: repeat(3, 1.4rem);
        gap: 0.15rem;
    }

    .anchors button {
        block-size: 1.4rem;
        border: 1px solid var(--line);
        border-radius: 4px;
        background: var(--ink-800);
        cursor: pointer;
    }

    .anchors button.on {
        border-color: var(--accent);
        background: var(--accent-bed);
    }

    .actions {
        display: flex;
        justify-content: flex-end;
        gap: 0.4rem;
    }

    .actions button {
        padding: 0.3rem 0.75rem;
        border: 1px solid var(--line);
        border-radius: 6px;
        background: var(--ink-800);
        color: var(--paper-dim);
        cursor: pointer;
    }

    .actions .primary {
        border-color: var(--accent);
        background: var(--accent-bed);
        color: var(--accent);
    }

    .actions button:disabled {
        opacity: 0.4;
        cursor: default;
    }

    button:focus-visible,
    input:focus-visible {
        outline: 2px solid var(--accent);
        outline-offset: 1px;
    }
</style>
