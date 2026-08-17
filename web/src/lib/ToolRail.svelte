<script lang="ts">
    import ToolIcon from "./ToolIcon.svelte";
    import { tools } from "./tools";
    import type { ToolId } from "./tools";
    import {
        wobbleFrameCount,
        wobbleIntervalMs,
        wobbleOutline,
    } from "./wobbleOutline";

    let {
        tool,
        hasSelection,
        canPaste,
        onselect,
        onselectionaction,
    }: {
        tool: ToolId;
        hasSelection: boolean;
        canPaste: boolean;
        onselect: (tool: ToolId) => void;
        onselectionaction: (
            action:
                | "all"
                | "invert"
                | "fill"
                | "delete"
                | "deselect"
                | "copy"
                | "cut"
                | "paste",
        ) => void;
    } = $props();

    const buttonSize = 44;
    let wobbleFrame = $state(0);

    const selectionActions = [
        {
            id: "all" as const,
            icon: "selectAll",
            label: "Select all",
            shortcut: "Ctrl A",
        },
        {
            id: "invert" as const,
            icon: "invert",
            label: "Invert selection",
            shortcut: "Ctrl Shift I",
        },
        {
            id: "fill" as const,
            icon: "bucket",
            label: "Fill selection",
            shortcut: "Alt Delete",
        },
        {
            id: "delete" as const,
            icon: "delete",
            label: "Delete selection",
            shortcut: "Delete",
        },
        {
            id: "copy" as const,
            icon: "copy",
            label: "Copy to a new layer",
            shortcut: "Ctrl C",
        },
        {
            id: "cut" as const,
            icon: "cut",
            label: "Cut selection",
            shortcut: "Ctrl X",
        },
        {
            id: "deselect" as const,
            icon: "deselect",
            label: "Deselect",
            shortcut: "Ctrl D",
        },
    ];

    const frames = Array.from({ length: wobbleFrameCount }, (_, frame) =>
        wobbleOutline(buttonSize, frame),
    );
    const outline = $derived(frames[wobbleFrame] ?? frames[0]);

    $effect(() => {
        // A ring that redraws itself is motion; anyone who asked for less of
        // it gets the first frame and no timer at all.
        if (window.matchMedia("(prefers-reduced-motion: reduce)").matches) {
            return;
        }
        const timer = setInterval(() => {
            wobbleFrame = (wobbleFrame + 1) % wobbleFrameCount;
        }, wobbleIntervalMs);
        return () => clearInterval(timer);
    });
</script>

<nav class="rail" aria-label="Tools">
    <ul>
        {#each tools as definition (definition.id)}
            <li>
                <button
                    id={`tool-${definition.id}`}
                    class="tool"
                    class:active={tool === definition.id}
                    aria-pressed={tool === definition.id}
                    title={`${definition.label} (${definition.shortcut}) — ${definition.hint}`}
                    onclick={() => onselect(definition.id)}
                >
                    {#if tool === definition.id}
                        <svg
                            class="ring"
                            viewBox={`0 0 ${buttonSize} ${buttonSize}`}
                            aria-hidden="true"
                        >
                            <path d={outline} />
                        </svg>
                    {/if}
                    <ToolIcon name={definition.id} />
                    <span class="key">{definition.shortcut}</span>
                </button>
            </li>
        {/each}
    </ul>

    {#if hasSelection}
        <div class="group" id="selection-actions">
            <span class="group-label">Selection</span>
            {#each selectionActions as action (action.id)}
                <button
                    id={`selection-${action.id}`}
                    class="tool compact"
                    title={`${action.label} (${action.shortcut})`}
                    onclick={() => onselectionaction(action.id)}
                >
                    <ToolIcon name={action.icon} size={18} />
                </button>
            {/each}
        </div>
    {/if}

    <!-- Outside the selection group: pasting needs a clipboard, not a
         selection, and on a phone there is no Ctrl+V to fall back on. -->
    {#if canPaste}
        <div class="group" id="clipboard-actions">
            <span class="group-label">Clip</span>
            <button
                id="selection-paste"
                class="tool compact"
                title="Paste as a new layer (Ctrl V)"
                onclick={() => onselectionaction("paste")}
            >
                <ToolIcon name="paste" size={18} />
            </button>
        </div>
    {/if}
</nav>

<style>
    .rail {
        display: flex;
        /* Never squeezed: the canvas column is the one that flexes. */
        flex: none;
        flex-direction: column;
        align-items: center;
        gap: 0.75rem;
        inline-size: 4rem;
        padding-block: 0.7rem;
        background: var(--ink-900);
        border-inline-end: 1px solid var(--line);
        overflow-y: auto;
        scrollbar-width: none;
    }

    ul {
        display: flex;
        flex-direction: column;
        gap: 0.3rem;
        margin: 0;
        padding: 0;
        list-style: none;
    }

    .tool {
        position: relative;
        display: flex;
        flex-direction: column;
        align-items: center;
        justify-content: center;
        gap: 0.1rem;
        inline-size: 2.75rem;
        block-size: 2.75rem;
        padding: 0;
        border: 0;
        border-radius: 12px;
        background: transparent;
        color: var(--paper-dim);
        cursor: pointer;
        transition:
            color 120ms ease,
            background-color 120ms ease;
    }

    .tool:hover {
        background: var(--ink-750);
        color: var(--paper);
    }

    .tool:focus-visible {
        outline: 2px solid var(--accent);
        outline-offset: 2px;
    }

    .tool.active {
        color: var(--accent);
        background: var(--accent-bed);
    }

    .ring {
        position: absolute;
        inset: 0;
        inline-size: 100%;
        block-size: 100%;
        fill: none;
        stroke: var(--accent);
        stroke-width: 1.4;
        pointer-events: none;
    }

    .key {
        font-size: 0.5625rem;
        font-weight: 700;
        letter-spacing: 0.06em;
        opacity: 0.62;
    }

    .group {
        display: flex;
        flex-direction: column;
        align-items: center;
        gap: 0.2rem;
        padding-block-start: 0.7rem;
        border-block-start: 1px solid var(--line);
        inline-size: 2.75rem;
    }

    .group-label {
        margin-block-end: 0.15rem;
        font-size: 0.5rem;
        font-weight: 700;
        letter-spacing: 0.1em;
        text-transform: uppercase;
        color: var(--paper-faint);
    }

    .tool.compact {
        block-size: 2.1rem;
        border-radius: 9px;
    }
</style>
