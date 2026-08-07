export interface RecoverySnapshot {
    name: string;
    bytes: ArrayBuffer;
    savedAt: number;
}

const databaseName = "ugurugu-web";
const storeName = "recovery";
const slotKey = "slot";

function requestToPromise<T>(request: IDBRequest<T>): Promise<T> {
    return new Promise((resolve, reject) => {
        request.onsuccess = () => resolve(request.result);
        request.onerror = () =>
            reject(request.error ?? new Error("IndexedDB request failed"));
    });
}

function openDatabase(): Promise<IDBDatabase> {
    if (typeof indexedDB === "undefined") {
        return Promise.reject(
            new Error("IndexedDB is unavailable in this browser"),
        );
    }
    return new Promise((resolve, reject) => {
        const request = indexedDB.open(databaseName, 1);
        request.onupgradeneeded = () => {
            if (!request.result.objectStoreNames.contains(storeName)) {
                request.result.createObjectStore(storeName);
            }
        };
        request.onsuccess = () => resolve(request.result);
        request.onerror = () =>
            reject(request.error ?? new Error("IndexedDB open failed"));
        request.onblocked = () =>
            reject(new Error("IndexedDB open is blocked by another tab"));
    });
}

async function withStore<T>(
    mode: IDBTransactionMode,
    operation: (store: IDBObjectStore) => IDBRequest<T>,
): Promise<T> {
    const database = await openDatabase();
    try {
        const transaction = database.transaction(storeName, mode);
        const result = await requestToPromise(
            operation(transaction.objectStore(storeName)),
        );
        await new Promise<void>((resolve, reject) => {
            transaction.oncomplete = () => resolve();
            transaction.onabort = () =>
                reject(
                    transaction.error ?? new Error("IndexedDB write aborted"),
                );
            transaction.onerror = () =>
                reject(
                    transaction.error ?? new Error("IndexedDB write failed"),
                );
        });
        return result;
    } finally {
        database.close();
    }
}

export async function readRecoverySnapshot(): Promise<RecoverySnapshot | null> {
    const record = await withStore("readonly", (store) => store.get(slotKey));
    if (
        !record ||
        typeof record !== "object" ||
        !((record as RecoverySnapshot).bytes instanceof ArrayBuffer)
    ) {
        return null;
    }
    return record as RecoverySnapshot;
}

export async function writeRecoverySnapshot(
    snapshot: RecoverySnapshot,
): Promise<void> {
    await withStore("readwrite", (store) => store.put(snapshot, slotKey));
}

export async function clearRecoverySnapshot(): Promise<void> {
    await withStore("readwrite", (store) => store.delete(slotKey));
}
