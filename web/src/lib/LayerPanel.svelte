<script lang="ts">
    import type { LayerInfo } from "./EngineClient";

    let {
        layers,
        onactivate,
        onvisible,
        onopacity,
        onadd,
        onremove,
        onrename,
        onmove,
    }: {
        layers: LayerInfo[];
        onactivate: (index: number) => void;
        onvisible: (index: number, visible: boolean) => void;
        onopacity: (index: number, opacity: number) => void;
        onadd: () => void;
        onremove: (index: number) => void;
        onrename: (index: number, name: string) => void;
        onmove: (index: number, offset: number) => void;
    } = $props();

    const displayLayers = $derived([...layers].reverse());
    const activeLayer = $derived(layers.find((layer) => layer.active));

    function rename(layer: LayerInfo) {
        const name = window.prompt("레이어 이름", layer.name);
        if (name !== null && name.trim() !== "" && name !== layer.name) {
            onrename(layer.index, name.trim());
        }
    }
</script>

<aside>
    <div class="panel-header">
        <h2>레이어</h2>
        <div class="panel-actions">
            <button id="layer-add" title="레이어 추가" onclick={onadd}>
                +
            </button>
            <button
                id="layer-remove"
                title="레이어 삭제"
                disabled={!activeLayer || layers.length < 2}
                onclick={() => activeLayer && onremove(activeLayer.index)}
            >
                −
            </button>
            <button
                title="위로"
                disabled={!activeLayer}
                onclick={() => activeLayer && onmove(activeLayer.index, 1)}
            >
                ↑
            </button>
            <button
                title="아래로"
                disabled={!activeLayer}
                onclick={() => activeLayer && onmove(activeLayer.index, -1)}
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
                    title="표시"
                    checked={layer.visible}
                    onchange={(event) =>
                        onvisible(
                            layer.index,
                            (event.currentTarget as HTMLInputElement).checked,
                        )}
                />
                <button
                    class="name"
                    class:group={layer.group}
                    onclick={() => onactivate(layer.index)}
                    ondblclick={() => rename(layer)}
                >
                    {layer.group ? "📁 " : ""}{layer.name}
                </button>
            </li>
            {#if layer.active && !layer.group}
                <li class="opacity-row">
                    <label>
                        불투명도
                        <input
                            type="range"
                            min="0"
                            max="100"
                            value={Math.round(layer.opacity * 100)}
                            onchange={(event) =>
                                onopacity(
                                    layer.index,
                                    Number(
                                        (event.currentTarget as HTMLInputElement)
                                            .value,
                                    ) / 100,
                                )}
                        />
                        <span>{Math.round(layer.opacity * 100)}%</span>
                    </label>
                </li>
            {/if}
        {/each}
    </ul>
</aside>

<style>
    aside {
        inline-size: 15rem;
        display: flex;
        flex-direction: column;
        background: #26292f;
        border-inline-start: 1px solid #3c4047;
        overflow-y: auto;
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

    .panel-actions {
        display: flex;
        gap: 0.3rem;
    }

    .panel-actions button {
        inline-size: 1.8rem;
        padding: 0.15rem 0;
        border: 1px solid #3c4047;
        border-radius: 5px;
        background: #2f333a;
        color: inherit;
        cursor: pointer;
    }

    .panel-actions button:disabled {
        opacity: 0.45;
        cursor: default;
    }

    ul {
        margin: 0;
        padding: 0.4rem;
        list-style: none;
        display: flex;
        flex-direction: column;
        gap: 0.25rem;
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
        background: #34415a;
        outline: 1px solid #4f8ef7;
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

    .opacity-row {
        padding-block: 0;
    }

    .opacity-row label {
        flex: 1;
        display: flex;
        align-items: center;
        gap: 0.4rem;
        font-size: 0.78rem;
        color: #9aa0a6;
    }

    .opacity-row input[type="range"] {
        flex: 1;
    }

    .opacity-row span {
        min-width: 2.4rem;
        text-align: right;
        font-variant-numeric: tabular-nums;
    }
</style>
