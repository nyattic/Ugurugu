export interface DocumentMeta {
    schemaVersion: number;
    width: number;
    height: number;
    frameCount: number;
    layerCount: number;
    fps: number;
}

export interface RenderedFrame {
    frame: number;
    width: number;
    height: number;
    pixels: Uint8ClampedArray;
}

interface PendingRequest {
    resolve: (value: never) => void;
    reject: (reason: Error) => void;
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

    async open(bytes: ArrayBuffer): Promise<DocumentMeta> {
        const response = await this.#request<{ meta: DocumentMeta }>(
            { type: "open", bytes },
            [bytes],
        );
        return response.meta;
    }

    async renderFrame(frame: number): Promise<RenderedFrame> {
        const response = await this.#request<{
            frame: number;
            width: number;
            height: number;
            pixels: ArrayBuffer;
        }>({ type: "render", frame });
        return {
            frame: response.frame,
            width: response.width,
            height: response.height,
            pixels: new Uint8ClampedArray(response.pixels),
        };
    }

    async serialize(): Promise<ArrayBuffer> {
        const response = await this.#request<{ bytes: ArrayBuffer }>({
            type: "serialize",
        });
        return response.bytes;
    }
}
