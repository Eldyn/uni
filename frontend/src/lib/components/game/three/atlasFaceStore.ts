/**
 * @file atlasFaceStore.ts
 * @brief Persistent store for baked card-face pixels, keyed by the
 *        card-definition content hash the atlas already uses.
 *
 * Sits behind cardFaceAtlas's in-memory bake cache: the atlas hydrates
 * stored pixels on load and writes back after a fresh bake, so a repeat
 * session skips re-baking. Owns the byte-size cap (LRU eviction) and hash
 * pruning; the atlas owns when pixels are written/read and which hashes
 * count as live.
 */

export interface StoredFace {
	/** Card-definition content hash (`faceKeyHash`, plus a layer suffix). */
	hash: string;
	width: number;
	height: number;
	/** Final composited RGBA pixels: 4 bytes per pixel, row-major. */
	data: Uint8Array;
	/** Opaque caller payload (the atlas re-bake key); never read here. */
	meta?: unknown;
	/** Epoch ms of the last read/write — the LRU recency marker. */
	lastUsed: number;
}

/** Minimal key-value contract both storage backends implement. */
export interface FaceStoreBackend {
	read(hash: string): Promise<StoredFace | undefined>;
	readAll(): Promise<StoredFace[]>;
	write(record: StoredFace): Promise<void>;
	remove(hash: string): Promise<void>;
}

export interface FaceStoreOptions {
	maxBytes?: number;
	/** Injectable clock so LRU recency is deterministic under test. */
	now?: () => number;
}

/**
 * INFO: 8 MiB, the simplest open choice — a baked face is ~10 KB of raw
 * RGBA (43x60x4), so the cap holds roughly 800 faces before LRU eviction.
 */
export const DEFAULT_FACE_STORE_MAX_BYTES = 8 * 1024 * 1024;

/** In-memory fake backend: the test double for IndexedDB. */
export function createMemoryFaceStoreBackend(): FaceStoreBackend {
	const rows = new Map<string, StoredFace>();
	return {
		async read(hash) {
			const row = rows.get(hash);
			// Copy out so callers cannot alias the stored bytes.
			return row ? { ...row, data: row.data.slice() } : undefined;
		},
		async readAll() {
			return [...rows.values()].map((row) => ({ ...row, data: row.data.slice() }));
		},
		async write(record) {
			rows.set(record.hash, { ...record, data: record.data.slice() });
		},
		async remove(hash) {
			rows.delete(hash);
		}
	};
}

const FACE_DB_NAME = "uni-card-faces";
const FACE_STORE_NAME = "faces";

function requestToPromise<T>(request: IDBRequest<T>): Promise<T> {
	return new Promise((resolve, reject) => {
		request.onsuccess = () => resolve(request.result);
		request.onerror = () => reject(request.error ?? new Error("indexeddb request failed"));
	});
}

/**
 * IndexedDB backend. INFO: when `indexedDB` is unavailable (SSR, jsdom,
 * exotic privacy modes) this returns the in-memory backend instead of
 * throwing — persistence then degrades to session-only, which is the
 * simplest fallback that keeps the same interface.
 */
export function createIndexedDbFaceStoreBackend(): FaceStoreBackend {
	if (typeof indexedDB === "undefined") return createMemoryFaceStoreBackend();

	let dbPromise: Promise<IDBDatabase> | null = null;
	const openDb = (): Promise<IDBDatabase> => {
		dbPromise ??= new Promise((resolve, reject) => {
			const open = indexedDB.open(FACE_DB_NAME, 1);
			open.onupgradeneeded = () => {
				open.result.createObjectStore(FACE_STORE_NAME, { keyPath: "hash" });
			};
			open.onsuccess = () => resolve(open.result);
			open.onerror = () => reject(open.error ?? new Error("indexeddb open failed"));
		});
		return dbPromise;
	};
	const withStore = async <T>(
		mode: IDBTransactionMode,
		run: (store: IDBObjectStore) => IDBRequest<T>
	): Promise<T> => {
		const db = await openDb();
		const tx = db.transaction(FACE_STORE_NAME, mode);
		return requestToPromise(run(tx.objectStore(FACE_STORE_NAME)));
	};
	return {
		read: (hash) => withStore("readonly", (store) => store.get(hash) as IDBRequest<StoredFace>),
		readAll: () => withStore("readonly", (store) => store.getAll() as IDBRequest<StoredFace[]>),
		write: async (record) => {
			await withStore("readwrite", (store) => store.put(record));
		},
		remove: async (hash) => {
			await withStore("readwrite", (store) => store.delete(hash));
		}
	};
}

/**
 * @brief LRU + byte-cap cache over a face backend.
 *
 * All operations run through one queue so concurrent bakes cannot interleave
 * with an eviction pass. INFO: the cap is enforced by re-reading the table on
 * each write; card pools stay small enough that this beats incremental
 * bookkeeping, and recency only needs session granularity because the atlas's
 * in-memory bake cache absorbs same-session reads.
 */
export class PersistentFaceStore {
	private readonly backend: FaceStoreBackend;
	private readonly maxBytes: number;
	private readonly now: () => number;
	private queue: Promise<unknown> = Promise.resolve();

	constructor(backend: FaceStoreBackend, options: FaceStoreOptions = {}) {
		this.backend = backend;
		this.maxBytes = options.maxBytes ?? DEFAULT_FACE_STORE_MAX_BYTES;
		this.now = options.now ?? (() => Date.now());
	}

	private enqueue<T>(op: () => Promise<T>): Promise<T> {
		const run = this.queue.then(op, op);
		this.queue = run.then(
			() => undefined,
			() => undefined
		);
		return run;
	}

	/** Reads a face and marks it most recently used. */
	async get(hash: string): Promise<StoredFace | undefined> {
		return this.enqueue(async () => {
			const row = await this.backend.read(hash);
			if (!row) return undefined;
			const touched = { ...row, lastUsed: this.now() };
			await this.backend.write(touched);
			return touched;
		});
	}

	/** Writes a face (stamping recency) and enforces the byte cap. */
	put(record: Omit<StoredFace, "lastUsed">): Promise<void> {
		return this.enqueue(async () => {
			await this.backend.write({ ...record, lastUsed: this.now() });
			await this.enforceCap();
		});
	}

	/** All stored faces, for hydration; does not touch recency. */
	entries(): Promise<StoredFace[]> {
		return this.enqueue(() => this.backend.readAll());
	}

	/** Deletes every face whose hash is absent from `liveHashes`. */
	prune(liveHashes: ReadonlySet<string>): Promise<number> {
		return this.enqueue(async () => {
			const rows = await this.backend.readAll();
			let removed = 0;
			for (const row of rows) {
				if (liveHashes.has(row.hash)) continue;
				await this.backend.remove(row.hash);
				removed++;
			}
			return removed;
		});
	}

	private async enforceCap(): Promise<void> {
		const rows = await this.backend.readAll();
		let total = rows.reduce((sum, row) => sum + row.data.byteLength, 0);
		if (total <= this.maxBytes) return;
		const oldestFirst = [...rows].sort((a, b) => a.lastUsed - b.lastUsed);
		let remaining = oldestFirst.length;
		for (const row of oldestFirst) {
			// INFO: always keep the newest record, so a single oversized face
			// persists instead of being written and dropped in one breath.
			if (total <= this.maxBytes || remaining <= 1) break;
			await this.backend.remove(row.hash);
			total -= row.data.byteLength;
			remaining--;
		}
	}
}
