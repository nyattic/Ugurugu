<script lang="ts">
    import type { BrushPresetInfo } from "./EngineClient";
    import type { ToolId } from "./tools";
    import type { ToolSettings } from "./ToolSettings";
    import {
        fillComparisons,
        fillReferences,
        toolDefinition,
    } from "./tools";

    let {
        tool,
        settings,
        presets,
        eraserPresets,
    }: {
        tool: ToolId;
        settings: ToolSettings;
        presets: BrushPresetInfo[];
        eraserPresets: BrushPresetInfo[];
    } = $props();

    const definition = $derived(toolDefinition(tool));
    const usesFlood = $derived(tool === "bucket" || tool === "wand");
    const toleranceEnabled = $derived(
        settings.fillComparison === fillComparisons.colorTolerance,
    );

    const references = [
        { value: fillReferences.activeLayer, label: "This layer" },
        { value: fillReferences.markedLayers, label: "Reference layers" },
        { value: fillReferences.allVisibleLayers, label: "All visible" },
    ];

    const shapes = [
        { value: "freehand", label: "Free" },
        { value: "rectangle", label: "Rect" },
        { value: "ellipse", label: "Oval" },
    ] as const;

    function onPresetChange(event: Event) {
        const index = Number((event.currentTarget as HTMLSelectElement).value);
        settings.presetIndex = index;
        const preset = presets[index];
        if (preset) {
            settings.brushSize = Math.round(preset.defaultSize);
        }
    }

    function onEraserPresetChange(event: Event) {
        const index = Number((event.currentTarget as HTMLSelectElement).value);
        settings.eraserPresetIndex = index;
        const preset = eraserPresets[index];
        if (preset) {
            settings.brushSize = Math.round(preset.defaultSize);
        }
    }
</script>

<section class="options" aria-label={`${definition.label} options`}>
    <header>
        <h2>{definition.label}</h2>
        <p>{definition.hint}</p>
    </header>

    {#if tool === "brush" || tool === "eraser"}
        <label class="field">
            <span class="field-label">Preset</span>
            {#if tool === "eraser"}
                <select
                    id="eraser-preset"
                    value={String(settings.eraserPresetIndex)}
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
                    id="brush-preset"
                    value={String(settings.presetIndex)}
                    onchange={onPresetChange}
                >
                    {#each presets as preset (preset.index)}
                        <option value={String(preset.index)}>
                            {preset.name}
                        </option>
                    {/each}
                </select>
            {/if}
        </label>

        <label class="field">
            <span class="field-label">
                Size <em>{settings.brushSize}px</em>
            </span>
            <input
                id="brush-size"
                type="range"
                min="1"
                max="64"
                bind:value={settings.brushSize}
            />
        </label>

        <label class="field">
            <span class="field-label">
                Smoothing <em>{settings.stabilization}%</em>
            </span>
            <input
                type="range"
                min="0"
                max="100"
                bind:value={settings.stabilization}
            />
        </label>

        {#if tool === "brush"}
            <label class="check">
                <input
                    id="brush-antialiasing"
                    type="checkbox"
                    bind:checked={settings.brushAntialiasing}
                />
                Antialias edges
            </label>
        {/if}
    {/if}

    {#if tool === "lasso"}
        <div class="field">
            <span class="field-label">Mode</span>
            <div class="segment" role="group" aria-label="Lasso mode">
                <button
                    id="lasso-mode-select"
                    class:on={settings.lassoMode === "select"}
                    aria-pressed={settings.lassoMode === "select"}
                    onclick={() => (settings.lassoMode = "select")}
                >
                    Select
                </button>
                <button
                    id="lasso-mode-paint"
                    class:on={settings.lassoMode === "paint"}
                    aria-pressed={settings.lassoMode === "paint"}
                    onclick={() => (settings.lassoMode = "paint")}
                >
                    Paint
                </button>
            </div>
        </div>

        <div class="field">
            <span class="field-label">Shape</span>
            <div class="segment" role="group" aria-label="Selection shape">
                {#each shapes as shape (shape.value)}
                    <button
                        id={`selection-shape-${shape.value}`}
                        class:on={settings.selectionShape === shape.value}
                        aria-pressed={settings.selectionShape === shape.value}
                        onclick={() => (settings.selectionShape = shape.value)}
                    >
                        {shape.label}
                    </button>
                {/each}
            </div>
        </div>

        <p class="note">
            Hold Shift to add to the selection, Alt to take away.
        </p>
    {/if}

    {#if usesFlood}
        <div class="field">
            <span class="field-label">Read from</span>
            <div class="stack" role="group" aria-label="Reference layers">
                {#each references as reference (reference.value)}
                    <button
                        id={`fill-reference-${reference.value}`}
                        class:on={settings.fillReference === reference.value}
                        aria-pressed={settings.fillReference === reference.value}
                        onclick={() =>
                            (settings.fillReference = reference.value)}
                    >
                        {reference.label}
                    </button>
                {/each}
            </div>
        </div>

        <div class="field">
            <span class="field-label">Edges</span>
            <div class="segment" role="group" aria-label="Fill comparison">
                <button
                    id="fill-comparison-alpha"
                    class:on={!toleranceEnabled}
                    aria-pressed={!toleranceEnabled}
                    onclick={() =>
                        (settings.fillComparison =
                            fillComparisons.alphaBoundary)}
                >
                    Lines
                </button>
                <button
                    id="fill-comparison-color"
                    class:on={toleranceEnabled}
                    aria-pressed={toleranceEnabled}
                    onclick={() =>
                        (settings.fillComparison =
                            fillComparisons.colorTolerance)}
                >
                    Color
                </button>
            </div>
        </div>

        <label class="field" class:disabled={!toleranceEnabled}>
            <span class="field-label">
                Tolerance <em>{settings.fillTolerance}</em>
            </span>
            <input
                id="fill-tolerance"
                type="range"
                min="0"
                max="255"
                disabled={!toleranceEnabled}
                bind:value={settings.fillTolerance}
            />
        </label>

        {#if tool === "bucket"}
            <label class="check">
                <input
                    id="bucket-antialiasing"
                    type="checkbox"
                    bind:checked={settings.bucketAntialiasing}
                />
                Antialias fill edge
            </label>
        {/if}
    {/if}

    {#if tool === "eyedropper"}
        <p class="note">
            Click or drag on the canvas to take a color. Sampling reads the
            composed frame, so it works on any layer.
        </p>
    {/if}
</section>

<style>
    .options {
        display: flex;
        flex-direction: column;
        gap: 0.85rem;
        inline-size: 13.5rem;
        padding: 0.9rem 0.85rem;
        background: var(--ink-850);
        border-inline-end: 1px solid var(--line);
        overflow-y: auto;
    }

    header {
        display: flex;
        flex-direction: column;
        gap: 0.2rem;
    }

    h2 {
        margin: 0;
        font-size: 0.6875rem;
        font-weight: 700;
        letter-spacing: 0.13em;
        text-transform: uppercase;
        color: var(--accent);
    }

    header p,
    .note {
        margin: 0;
        font-size: 0.75rem;
        line-height: 1.45;
        color: var(--paper-faint);
    }

    .note {
        padding-block-start: 0.1rem;
    }

    .field {
        display: flex;
        flex-direction: column;
        gap: 0.35rem;
    }

    .field.disabled {
        opacity: 0.4;
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

    .segment {
        display: flex;
        gap: 2px;
        padding: 2px;
        border-radius: 8px;
        background: var(--ink-950);
    }

    .segment button {
        flex: 1;
        padding: 0.3rem 0.1rem;
        border: 0;
        border-radius: 6px;
        background: transparent;
        color: var(--paper-dim);
        font: inherit;
        font-size: 0.75rem;
        cursor: pointer;
    }

    .stack {
        display: flex;
        flex-direction: column;
        gap: 2px;
        padding: 2px;
        border-radius: 8px;
        background: var(--ink-950);
    }

    .stack button {
        padding: 0.32rem 0.5rem;
        border: 0;
        border-radius: 6px;
        background: transparent;
        color: var(--paper-dim);
        font: inherit;
        font-size: 0.75rem;
        text-align: start;
        cursor: pointer;
    }

    .segment button:hover,
    .stack button:hover {
        color: var(--paper);
    }

    .segment button.on,
    .stack button.on {
        background: var(--accent-bed);
        color: var(--accent);
    }

    .segment button:focus-visible,
    .stack button:focus-visible {
        outline: 2px solid var(--accent);
        outline-offset: -2px;
    }

    .check {
        display: flex;
        align-items: center;
        gap: 0.45rem;
        font-size: 0.78rem;
        color: var(--paper-dim);
        cursor: pointer;
    }

    select {
        inline-size: 100%;
        padding: 0.35rem 0.45rem;
        border: 1px solid var(--line);
        border-radius: 7px;
        background: var(--ink-800);
        color: var(--paper);
        font: inherit;
        font-size: 0.8rem;
        cursor: pointer;
    }

    input[type="range"] {
        inline-size: 100%;
        accent-color: var(--accent);
    }

    input[type="checkbox"] {
        accent-color: var(--accent);
    }
</style>
