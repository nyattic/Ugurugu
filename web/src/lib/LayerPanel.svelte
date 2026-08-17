<script lang="ts">
    import {
        layerBlendModes,
        mergeBlockReasons,
        type LayerBlendMode,
        type LayerInfo,
        type LayerThumbnail,
    } from "./EngineClient";

    let {
        layers,
        thumbnails,
        onactivate,
        onvisible,
        onreference,
        onopacity,
        onadd,
        onremove,
        onrename,
        onmove,
        onaddgroup,
        onduplicate,
        onmergedown,
        onclear,
        onblendmode,
        onclip,
        onparentgroup,
    }: {
        layers: LayerInfo[];
        thumbnails: LayerThumbnail[];
        // Layers are named by their stable id, not by the row index the click
        // happened to see: these commands are queued, and an earlier delete or
        // move renumbers every row before the next one runs.
        onactivate: (id: string) => void;
        onvisible: (id: string, visible: boolean) => void;
        onreference: (id: string, reference: boolean) => void;
        onopacity: (id: string, opacity: number) => void;
        onadd: () => void;
        onremove: (id: string) => void;
        onrename: (id: string, name: string) => void;
        onmove: (id: string, offset: number) => void;
        onaddgroup: (id: string | null) => void;
        onduplicate: (id: string) => void;
        onmergedown: (id: string) => void;
        onclear: (id: string) => void;
        onblendmode: (id: string, mode: LayerBlendMode) => void;
        onclip: (id: string, clipped: boolean) => void;
        onparentgroup: (id: string, groupId: string | null) => void;
    } = $props();

    const displayLayers = $derived([...layers].reverse());
    const activeLayer = $derived(layers.find((layer) => layer.active));
    const groups = $derived(
        layers.filter(
            (layer) => layer.group && layer.id !== activeLayer?.id,
        ),
    );
    const mergeReason = $derived(
        activeLayer && activeLayer.mergeStatus !== 0
            ? (mergeBlockReasons[activeLayer.mergeStatus] ??
              "This layer cannot be merged down.")
            : "Merge into the layer below",
    );

    function rename(layer: LayerInfo) {
        const name = window.prompt("Layer name", layer.name);
        if (name !== null && name.trim() !== "" && name !== layer.name) {
            onrename(layer.id, name.trim());
        }
    }

    function drawThumbnail(
        canvas: HTMLCanvasElement,
        thumbnail: LayerThumbnail | undefined,
    ) {
        function render(current: LayerThumbnail | undefined) {
            const context = canvas.getContext("2d");
            if (!context) {
                return;
            }
            if (!current?.pixels || current.width <= 0) {
                canvas.width = 1;
                canvas.height = 1;
                context.clearRect(0, 0, 1, 1);
                return;
            }
            canvas.width = current.width;
            canvas.height = current.height;
            context.putImageData(
                new ImageData(current.pixels, current.width, current.height),
                0,
                0,
            );
        }
        render(thumbnail);
        return { update: render };
    }
</script>

<aside>
    <div class="panel-header">
        <h2>Layers</h2>
        <div class="panel-actions">
            <button id="layer-add" title="Add layer" onclick={onadd}>
                +
            </button>
            <button
                id="layer-add-group"
                title="Put the active layer in a new group"
                onclick={() => onaddgroup(activeLayer?.id ?? null)}
            >
                ▣
            </button>
            <button
                id="layer-remove"
                title="Delete layer"
                disabled={!activeLayer || layers.length < 2}
                onclick={() => activeLayer && onremove(activeLayer.id)}
            >
                −
            </button>
            <button
                title="Move up"
                disabled={!activeLayer}
                onclick={() => activeLayer && onmove(activeLayer.id, 1)}
            >
                ↑
            </button>
            <button
                title="Move down"
                disabled={!activeLayer}
                onclick={() => activeLayer && onmove(activeLayer.id, -1)}
            >
                ↓
            </button>
        </div>
    </div>
    <ul>
        {#each displayLayers as layer (layer.index)}
            <li
                class:active={layer.active}
                style={`--depth: ${layer.depth}`}
            >
                <input
                    type="checkbox"
                    title="Visible"
                    checked={layer.visible}
                    onchange={(event) =>
                        onvisible(
                            layer.id,
                            (event.currentTarget as HTMLInputElement).checked,
                        )}
                />
                <span class="thumb-box">
                    <canvas
                        use:drawThumbnail={thumbnails.find(
                            (thumbnail) => thumbnail.index === layer.index,
                        )}
                    ></canvas>
                </span>
                <button
                    class="name"
                    class:group={layer.group}
                    onclick={() => onactivate(layer.id)}
                    ondblclick={() => rename(layer)}
                >
                    {layer.group ? "📁 " : ""}{layer.name}
                </button>
                {#if !layer.group}
                    <button
                        class="reference"
                        class:on={layer.reference}
                        title="Reference layer for the wand and the bucket"
                        aria-pressed={layer.reference}
                        onclick={() =>
                            onreference(layer.id, !layer.reference)}
                    >
                        R
                    </button>
                {/if}
            </li>
        {/each}
    </ul>
    <div class="layer-controls">
        <div class="row buttons">
            <button
                id="layer-duplicate"
                title="Duplicate layer"
                disabled={!activeLayer}
                onclick={() => activeLayer && onduplicate(activeLayer.id)}
            >
                Duplicate
            </button>
            <button
                id="layer-merge-down"
                title={mergeReason}
                disabled={!activeLayer || activeLayer.mergeStatus !== 0}
                onclick={() => activeLayer && onmergedown(activeLayer.id)}
            >
                Merge down
            </button>
            <button
                id="layer-clear"
                title="Erase everything on this layer"
                disabled={!activeLayer || activeLayer.group}
                onclick={() => activeLayer && onclear(activeLayer.id)}
            >
                Clear
            </button>
        </div>
        <label class="row">
            <span class="field-label">Blend</span>
            <select
                id="layer-blend-mode"
                disabled={!activeLayer}
                value={activeLayer?.blendMode ?? 0}
                onchange={(event) =>
                    activeLayer &&
                    onblendmode(
                        activeLayer.id,
                        Number(
                            (event.currentTarget as HTMLSelectElement).value,
                        ) as LayerBlendMode,
                    )}
            >
                {#each layerBlendModes as mode, index (mode)}
                    <option value={index}>{mode}</option>
                {/each}
            </select>
        </label>
        <label class="row">
            <span class="field-label">Group</span>
            <select
                id="layer-parent-group"
                disabled={!activeLayer}
                value={activeLayer && activeLayer.depth > 0 ? "" : "top"}
                onchange={(event) => {
                    const value = (event.currentTarget as HTMLSelectElement)
                        .value;
                    if (activeLayer) {
                        onparentgroup(
                            activeLayer.id,
                            value === "top" ? null : value,
                        );
                    }
                }}
            >
                <option value="top">Top level</option>
                {#each groups as group (group.id)}
                    <option value={group.id}>{group.name}</option>
                {/each}
            </select>
        </label>
        <label class="row checkbox">
            <input
                id="layer-clip"
                type="checkbox"
                disabled={!activeLayer || activeLayer.group}
                checked={activeLayer?.clipped ?? false}
                onchange={(event) =>
                    activeLayer &&
                    onclip(
                        activeLayer.id,
                        (event.currentTarget as HTMLInputElement).checked,
                    )}
            />
            Clip to layer below
        </label>
        <label>
            <span class="field-label">
                Opacity
                <em>
                    {activeLayer ? Math.round(activeLayer.opacity * 100) : 100}%
                </em>
            </span>
            <input
                id="layer-opacity"
                type="range"
                min="0"
                max="100"
                disabled={!activeLayer || activeLayer.group}
                value={activeLayer
                    ? Math.round(activeLayer.opacity * 100)
                    : 100}
                onchange={(event) =>
                    activeLayer &&
                    onopacity(
                        activeLayer.id,
                        Number(
                            (event.currentTarget as HTMLInputElement).value,
                        ) / 100,
                    )}
            />
        </label>
    </div>
</aside>

<style>
    aside {
        flex: 1;
        min-block-size: 0;
        display: flex;
        flex-direction: column;
        background: var(--ink-850);
        overflow: hidden;
    }

    .panel-header {
        display: flex;
        align-items: center;
        justify-content: space-between;
        padding: 0.6rem 0.75rem;
        border-block-end: 1px solid var(--line);
    }

    h2 {
        margin: 0;
        font-size: 0.625rem;
        font-weight: 700;
        letter-spacing: 0.13em;
        text-transform: uppercase;
        color: var(--paper-dim);
    }

    .panel-actions {
        display: flex;
        gap: 0.2rem;
    }

    .panel-actions button {
        inline-size: 1.7rem;
        padding: 0.12rem 0;
        border: 1px solid var(--line);
        border-radius: 6px;
        background: var(--ink-800);
        color: var(--paper-dim);
        cursor: pointer;
    }

    .panel-actions button:hover:not(:disabled) {
        background: var(--ink-750);
        color: var(--paper);
    }

    .panel-actions button:focus-visible {
        outline: 2px solid var(--accent);
        outline-offset: 1px;
    }

    .panel-actions button:disabled {
        opacity: 0.38;
        cursor: default;
    }

    ul {
        flex: 1;
        min-block-size: 0;
        margin: 0;
        padding: 0.4rem;
        list-style: none;
        display: flex;
        flex-direction: column;
        gap: 0.25rem;
        overflow-y: auto;
    }

    li {
        display: flex;
        align-items: center;
        gap: 0.45rem;
        padding: 0.3rem 0.5rem;
        padding-inline-start: calc(0.5rem + var(--depth, 0) * 0.9rem);
        border-radius: 6px;
    }

    li.active {
        background: var(--accent-bed);
        outline: 1px solid rgba(255, 201, 74, 0.42);
    }

    li input[type="checkbox"] {
        accent-color: var(--accent);
    }

    .thumb-box {
        flex-shrink: 0;
        inline-size: 48px;
        block-size: 32px;
        display: flex;
        align-items: center;
        justify-content: center;
        border: 1px solid var(--line);
        border-radius: 4px;
        background:
            repeating-conic-gradient(var(--ink-750) 0% 25%, var(--ink-800) 0% 50%)
            0 0 / 12px 12px;
        overflow: hidden;
    }

    .thumb-box canvas {
        max-width: 100%;
        max-height: 100%;
    }

    .name {
        flex: 1;
        padding: 0.15rem 0.2rem;
        border: 0;
        background: none;
        color: inherit;
        font-size: 0.88rem;
        text-align: start;
        cursor: pointer;
        overflow: hidden;
        text-overflow: ellipsis;
        white-space: nowrap;
    }

    .reference {
        flex-shrink: 0;
        inline-size: 1.35rem;
        padding: 0.1rem 0;
        border: 1px solid var(--line);
        border-radius: 4px;
        background: none;
        color: var(--paper-dim);
        font-size: 0.6875rem;
        font-weight: 700;
        line-height: 1.2;
        cursor: pointer;
    }

    .reference.on {
        border-color: var(--accent);
        background: var(--accent-bed);
        color: var(--accent);
    }

    .reference:focus-visible {
        outline: 2px solid var(--accent);
        outline-offset: 1px;
    }

    .layer-controls {
        display: flex;
        flex-direction: column;
        gap: 0.45rem;
        padding: 0.5rem 0.75rem;
        border-block-start: 1px solid var(--line);
    }

    .layer-controls label {
        display: flex;
        flex-direction: column;
        gap: 0.35rem;
    }

    .layer-controls .row {
        flex-direction: row;
        align-items: center;
        gap: 0.45rem;
    }

    .layer-controls .row .field-label {
        flex: none;
        min-inline-size: 3.2rem;
    }

    .layer-controls select {
        flex: 1;
        min-inline-size: 0;
    }

    .layer-controls .checkbox {
        font-size: 0.75rem;
        color: var(--paper-dim);
    }

    .buttons button {
        flex: 1;
        padding: 0.3rem 0.2rem;
        font-size: 0.6875rem;
    }

    .field-label {
        display: flex;
        align-items: baseline;
        justify-content: space-between;
        font-size: 0.625rem;
        font-weight: 700;
        letter-spacing: 0.1em;
        text-transform: uppercase;
        color: var(--paper-dim);
    }

    .field-label em {
        font-family: var(--mono);
        font-size: 0.6875rem;
        font-style: normal;
        font-variant-numeric: tabular-nums;
        letter-spacing: 0;
        text-transform: none;
        color: var(--paper);
    }

    .layer-controls input[type="range"] {
        inline-size: 100%;
        accent-color: var(--accent);
    }
</style>
