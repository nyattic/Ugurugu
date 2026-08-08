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
        <span class="current" style={`background: ${color}`}></span>
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
        background: #26292f;
        border-block-end: 1px solid #3c4047;
    }

    .panel-header {
        display: flex;
        align-items: center;
        justify-content: space-between;
        padding: 0.6rem 0.8rem;
        border-block-end: 1px solid #3c4047;
    }

    h2 {
        margin: 0;
        font-size: 0.95rem;
        font-weight: 600;
    }

    .current {
        inline-size: 1.6rem;
        block-size: 1.2rem;
        border: 1px solid #4a4f57;
        border-radius: 4px;
    }

    .recent {
        display: flex;
        flex-direction: column;
        gap: 0.3rem;
        padding-inline: 0.8rem;
    }

    .recent-label {
        font-size: 0.78rem;
        color: #9aa0a6;
    }

    .swatches {
        display: flex;
        align-items: center;
        flex-wrap: wrap;
        gap: 0.25rem;
    }

    .swatch {
        inline-size: 1.35rem;
        block-size: 1.35rem;
        padding: 0;
        border: 1px solid #4a4f57;
        border-radius: 4px;
        cursor: pointer;
    }

    .swatch.selected {
        outline: 2px solid #4f8ef7;
        outline-offset: 1px;
    }
</style>
