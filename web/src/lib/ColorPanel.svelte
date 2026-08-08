<script lang="ts">
    import ColorWheel from "./ColorWheel.svelte";

    let {
        color,
        recentColors,
        onchange,
        onrecent,
    }: {
        color: string;
        recentColors: string[];
        onchange: (hex: string) => void;
        onrecent: (hex: string) => void;
    } = $props();
</script>

<section class="color-panel">
    <div class="panel-header">
        <h2>Color</h2>
        <div class="current">
            <span class="value">{color}</span>
            <input
                type="color"
                value={color}
                title="Pick a color"
                aria-label="Brush color"
                oninput={(event) =>
                    onchange((event.currentTarget as HTMLInputElement).value)}
            />
        </div>
    </div>
    <ColorWheel {color} {onchange} />
    {#if recentColors.length > 0}
        <div class="recent">
            <span class="recent-label">Recent</span>
            <div class="swatches" id="recent-colors">
                {#each recentColors as recentColor (recentColor)}
                    <button
                        class="swatch"
                        class:selected={color === recentColor}
                        style={`background: ${recentColor}`}
                        title={recentColor}
                        aria-label={`Recent color ${recentColor}`}
                        onclick={() => onrecent(recentColor)}
                    ></button>
                {/each}
            </div>
        </div>
    {/if}
</section>

<style>
    .color-panel {
        display: flex;
        flex-direction: column;
        gap: 0.5rem;
        padding-block-end: 0.6rem;
        background: var(--ink-850);
        border-block-end: 1px solid var(--line);
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

    .current {
        display: flex;
        align-items: center;
        gap: 0.4rem;
    }

    .value {
        font-family: var(--mono);
        font-size: 0.6875rem;
        color: var(--paper-faint);
    }

    .current input[type="color"] {
        inline-size: 1.8rem;
        block-size: 1.4rem;
        padding: 0;
        border: 1px solid var(--line);
        border-radius: 5px;
        background: none;
        cursor: pointer;
    }

    .recent {
        display: flex;
        flex-direction: column;
        gap: 0.3rem;
        padding-inline: 0.75rem;
    }

    .recent-label {
        font-size: 0.625rem;
        font-weight: 700;
        letter-spacing: 0.1em;
        text-transform: uppercase;
        color: var(--paper-dim);
    }

    .swatches {
        display: flex;
        align-items: center;
        flex-wrap: wrap;
        gap: 0.25rem;
    }

    .swatch {
        inline-size: 1.3rem;
        block-size: 1.3rem;
        padding: 0;
        border: 1px solid var(--line);
        border-radius: 5px;
        cursor: pointer;
    }

    .swatch.selected {
        outline: 2px solid var(--accent);
        outline-offset: 1px;
    }
</style>
