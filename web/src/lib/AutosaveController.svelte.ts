import {
    clearRecoverySnapshot,
    readRecoverySnapshot,
    writeRecoverySnapshot,
} from "./RecoveryStore";
import type { RecoverySnapshot } from "./RecoveryStore";

interface AutosaveHost {
    // Whether a snapshot may be taken at all: there has to be a document, and
    // a stroke in progress must not be interrupted by a serialize.
    ready: () => boolean;
    // Bumped by every change worth saving; a snapshot that matches the last
    // saved revision is skipped.
    revision: () => number;
    name: () => string;
    serialize: () => Promise<ArrayBuffer>;
    open: (bytes: ArrayBuffer, name: string) => void;
}

// The IndexedDB recovery slot: one snapshot, written on an interval and when
// the tab is hidden, offered back on the next visit.
export class AutosaveController {
    offer = $state<RecoverySnapshot | null>(null);
    status = $state("");

    #host: AutosaveHost;
    #savedRevision = 0;
    #busy = false;

    constructor(host: AutosaveHost) {
        this.#host = host;
    }

    // Called when a document is adopted: nothing of the old one is worth
    // saving against the new one's revisions.
    reset() {
        this.#savedRevision = 0;
    }

    async readOffer() {
        try {
            this.offer = await readRecoverySnapshot();
        } catch (error) {
            this.status = `Could not read the recovery slot — ${error}`;
        }
    }

    async snapshot() {
        if (
            !this.#host.ready() ||
            this.#busy ||
            this.#host.revision() === this.#savedRevision
        ) {
            return;
        }
        this.#busy = true;
        const revision = this.#host.revision();
        try {
            const bytes = await this.#host.serialize();
            await writeRecoverySnapshot({
                name: this.#host.name(),
                bytes,
                savedAt: Date.now(),
            });
            this.#savedRevision = revision;
            const time = new Date().toLocaleTimeString();
            this.status = `Recovery snapshot saved ${time}`;
        } catch (error) {
            const detail =
                error instanceof Error
                    ? `${error.name}: ${error.message}`
                    : String(error);
            this.status = `Recovery save failed — ${detail}`;
        } finally {
            this.#busy = false;
        }
    }

    restore() {
        const offer = this.offer;
        this.offer = null;
        if (offer) {
            this.#host.open(offer.bytes, offer.name);
        }
    }

    async discard() {
        this.offer = null;
        try {
            await clearRecoverySnapshot();
        } catch (error) {
            this.status = `Could not clear the recovery slot — ${error}`;
        }
    }

    // Starts the interval and the hidden-tab hook; the returned function stops
    // both.
    start(): () => void {
        const timer = setInterval(() => {
            void this.snapshot();
        }, autosaveIntervalMs());
        const onHidden = () => {
            if (document.visibilityState === "hidden") {
                void this.snapshot();
            }
        };
        document.addEventListener("visibilitychange", onHidden);
        return () => {
            clearInterval(timer);
            document.removeEventListener("visibilitychange", onHidden);
        };
    }
}

// Reload-safety knob: the interval is short because a browser tab can go away
// without any reliable shutdown callback. Tests pass ?autosave=1.
export function autosaveIntervalMs() {
    const parameter = new URLSearchParams(window.location.search).get(
        "autosave",
    );
    const seconds = Number(parameter);
    if (!Number.isFinite(seconds) || seconds <= 0) {
        return 15000;
    }
    return Math.min(600, Math.max(1, seconds)) * 1000;
}
