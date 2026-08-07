export interface DocumentMeta {
    schemaVersion: number;
    width: number;
    height: number;
    frameCount: number;
    layerCount: number;
    fps: number;
    canUndo: boolean;
    canRedo: boolean;
}

export interface RenderedFrame {
    frame: number;
    width: number;
    height: number;
    pixels: Uint8ClampedArray;
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
    resolve: (value: never) => void;
    reject: (reason: Error) => void;
}

interface RenderResponse {
    frame: number;
    width: number;
    height: number;
    pixels: ArrayBuffer;
    canUndo: boolean;
    canRedo: boolean;
}

export class EngineClient {
    #worker: Worker;
    #pending = new Map<number, PendingRequest>();
    #nextId = 0;

    constructor() {
        this.#worker = new Worker("/engine/engine-worker.js");
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
            this.#pending.set(id, { resolve: resolve as never, reject });
            this.#worker.postMessage({ id, ...message }, transfer);
        });
    }

    static #toFrame(response: RenderResponse): RenderedFrame {
        return {
            frame: response.frame,
            width: response.width,
            height: response.height,
            pixels: new Uint8ClampedArray(response.pixels),
            canUndo: response.canUndo,
            canRedo: response.canRedo,
        };
    }

    async open(bytes: ArrayBuffer): Promise<DocumentMeta> {
        const response = await this.#request<{ meta: DocumentMeta }>(
            { type: "open", bytes },
            [bytes],
        );
        return response.meta;
    }

    async renderFrame(frame: number): Promise<RenderedFrame> {
        const response = await this.#request<RenderResponse>({
            type: "render",
            frame,
        });
        return EngineClient.#toFrame(response);
    }

    async setBrush(brush: BrushSettings): Promise<void> {
        await this.#request({ type: "brush", ...brush });
    }

    async strokeBegin(
        frame: number,
        x: number,
        y: number,
        pressure: number,
    ): Promise<RenderedFrame> {
        const response = await this.#request<RenderResponse>({
            type: "strokeBegin",
            frame,
            x,
            y,
            pressure,
        });
        return EngineClient.#toFrame(response);
    }

    async strokeAppend(
        frame: number,
        points: number[],
    ): Promise<RenderedFrame> {
        const response = await this.#request<RenderResponse>({
            type: "strokeAppend",
            frame,
            points,
        });
        return EngineClient.#toFrame(response);
    }

    async strokeEnd(frame: number): Promise<RenderedFrame> {
        const response = await this.#request<RenderResponse>({
            type: "strokeEnd",
            frame,
        });
        return EngineClient.#toFrame(response);
    }

    async undo(frame: number): Promise<RenderedFrame> {
        const response = await this.#request<RenderResponse>({
            type: "undo",
            frame,
        });
        return EngineClient.#toFrame(response);
    }

    async redo(frame: number): Promise<RenderedFrame> {
        const response = await this.#request<RenderResponse>({
            type: "redo",
            frame,
        });
        return EngineClient.#toFrame(response);
    }

    async serialize(): Promise<ArrayBuffer> {
        const response = await this.#request<{ bytes: ArrayBuffer }>({
            type: "serialize",
        });
        return response.bytes;
    }
}
