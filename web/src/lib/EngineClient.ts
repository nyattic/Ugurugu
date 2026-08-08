export interface LayerInfo {
    index: number;
    name: string;
    group: boolean;
    visible: boolean;
    opacity: number;
    active: boolean;
    depth: number;
}

export interface BrushPresetInfo {
    index: number;
    name: string;
    defaultSize: number;
}

export interface LayerThumbnail {
    index: number;
    width: number;
    height: number;
    pixels: Uint8ClampedArray<ArrayBuffer> | null;
}

export interface DocumentMeta {
    abiVersion: number;
    schemaVersion: number;
    width: number;
    height: number;
    frameCount: number;
    layerCount: number;
    fps: number;
    presets: BrushPresetInfo[];
    eraserPresets: BrushPresetInfo[];
    layers: LayerInfo[];
    canUndo: boolean;
    canRedo: boolean;
}

export interface RegionUpdate {
    rect: { x: number; y: number; width: number; height: number };
    pixels: Uint8ClampedArray<ArrayBuffer> | null;
    layers: LayerInfo[];
    canUndo: boolean;
    canRedo: boolean;
}

export interface BrushSettings {
    red: number;
    green: number;
    blue: number;
    alpha: number;
    width: number;
    erase: boolean;
}

interface PendingRequest {
    resolve: (value: unknown) => void;
    reject: (reason: Error) => void;
}

interface RegionResponse {
    rect: { x: number; y: number; width: number; height: number };
    pixels: ArrayBuffer | null;
    layers: LayerInfo[];
    canUndo: boolean;
    canRedo: boolean;
}

export class EngineClient {
    #worker: Worker;
    #pending = new Map<number, PendingRequest>();
    #nextId = 0;

    constructor() {
        // Relative to the page so the worker stays same-origin under
        // itch.io's project subdirectory as well as the dev server.
        this.#worker = new Worker(
            new URL("engine/engine-worker.js", document.baseURI),
        );
        this.#worker.onmessage = (event) => {
            const { id, ok, error } = event.data;
            const pending = this.#pending.get(id);
            if (!pending) {
                return;
            }
            this.#pending.delete(id);
            if (ok) {
                pending.resolve(event.data);
            } else {
                pending.reject(new Error(error));
            }
        };
    }

    #request<T>(
        message: Record<string, unknown>,
        transfer: Transferable[] = [],
    ): Promise<T> {
        const id = this.#nextId;
        this.#nextId += 1;
        return new Promise<T>((resolve, reject) => {
            this.#pending.set(id, {
                resolve: resolve as (value: unknown) => void,
                reject,
            });
            this.#worker.postMessage({ id, ...message }, transfer);
        });
    }

    static #toRegion(response: RegionResponse): RegionUpdate {
        return {
            rect: response.rect,
            pixels: response.pixels
                ? new Uint8ClampedArray(response.pixels)
                : null,
            layers: response.layers,
            canUndo: response.canUndo,
            canRedo: response.canRedo,
        };
    }

    async #regionRequest(
        message: Record<string, unknown>,
    ): Promise<RegionUpdate> {
        const response = await this.#request<RegionResponse>(message);
        return EngineClient.#toRegion(response);
    }

    async open(bytes: ArrayBuffer, undoLimit: number): Promise<DocumentMeta> {
        const response = await this.#request<{ meta: DocumentMeta }>(
            { type: "open", bytes, undoLimit },
            [bytes],
        );
        return response.meta;
    }

    async create(
        width: number,
        height: number,
        undoLimit: number,
    ): Promise<DocumentMeta> {
        const response = await this.#request<{ meta: DocumentMeta }>({
            type: "create",
            width,
            height,
            undoLimit,
        });
        return response.meta;
    }

    renderFrame(frame: number): Promise<RegionUpdate> {
        return this.#regionRequest({ type: "render", frame });
    }

    async setBrush(brush: BrushSettings): Promise<void> {
        await this.#request({ type: "brush", ...brush });
    }

    async setBrushPreset(index: number): Promise<void> {
        await this.#request({ type: "brushPreset", index });
    }

    async setEraserPreset(index: number): Promise<void> {
        await this.#request({ type: "eraserPreset", index });
    }

    async setStabilization(strength: number): Promise<void> {
        await this.#request({ type: "stabilization", strength });
    }

    strokeBegin(
        frame: number,
        x: number,
        y: number,
        pressure: number,
        timestamp: number,
    ): Promise<RegionUpdate> {
        return this.#regionRequest({
            type: "strokeBegin",
            frame,
            x,
            y,
            pressure,
            timestamp,
        });
    }

    strokeAppend(frame: number, points: number[]): Promise<RegionUpdate> {
        return this.#regionRequest({ type: "strokeAppend", frame, points });
    }

    strokeEnd(frame: number): Promise<RegionUpdate> {
        return this.#regionRequest({ type: "strokeEnd", frame });
    }

    undo(frame: number): Promise<RegionUpdate> {
        return this.#regionRequest({ type: "undo", frame });
    }

    redo(frame: number): Promise<RegionUpdate> {
        return this.#regionRequest({ type: "redo", frame });
    }

    layerActivate(frame: number, index: number): Promise<RegionUpdate> {
        return this.#regionRequest({ type: "layerActivate", frame, index });
    }

    layerVisible(
        frame: number,
        index: number,
        visible: boolean,
    ): Promise<RegionUpdate> {
        return this.#regionRequest({
            type: "layerVisible",
            frame,
            index,
            visible,
        });
    }

    layerOpacity(
        frame: number,
        index: number,
        opacity: number,
    ): Promise<RegionUpdate> {
        return this.#regionRequest({
            type: "layerOpacity",
            frame,
            index,
            opacity,
        });
    }

    layerAdd(frame: number): Promise<RegionUpdate> {
        return this.#regionRequest({ type: "layerAdd", frame });
    }

    layerRemove(frame: number, index: number): Promise<RegionUpdate> {
        return this.#regionRequest({ type: "layerRemove", frame, index });
    }

    layerRename(
        frame: number,
        index: number,
        name: string,
    ): Promise<RegionUpdate> {
        return this.#regionRequest({ type: "layerRename", frame, index, name });
    }

    layerMove(
        frame: number,
        index: number,
        offset: number,
    ): Promise<RegionUpdate> {
        return this.#regionRequest({ type: "layerMove", frame, index, offset });
    }

    async serialize(): Promise<ArrayBuffer> {
        const response = await this.#request<{ bytes: ArrayBuffer }>({
            type: "serialize",
        });
        return response.bytes;
    }

    async exportGif(): Promise<ArrayBuffer> {
        const response = await this.#request<{ bytes: ArrayBuffer }>({
            type: "exportGif",
        });
        return response.bytes;
    }

    async layerThumbnails(
        devicePixelRatio: number,
    ): Promise<LayerThumbnail[]> {
        const response = await this.#request<{
            thumbnails: Array<{
                index: number;
                width: number;
                height: number;
                pixels: ArrayBuffer | null;
            }>;
        }>({ type: "layerThumbnails", devicePixelRatio });
        return response.thumbnails.map((thumbnail) => ({
            index: thumbnail.index,
            width: thumbnail.width,
            height: thumbnail.height,
            pixels: thumbnail.pixels
                ? new Uint8ClampedArray(thumbnail.pixels)
                : null,
        }));
    }
}
