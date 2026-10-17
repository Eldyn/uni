import { describe, it, expect } from "vitest";
import {
	PersistentFaceStore,
	createIndexedDbFaceStoreBackend,
	createMemoryFaceStoreBackend,
	type StoredFace
} from "$components/game/three/atlasFaceStore";

const FACE_BYTES = 43 * 60 * 4;

function record(hash: string, bytes: number, meta?: unknown): Omit<StoredFace, "lastUsed"> {
	return { hash, width: 43, height: 60, data: new Uint8Array(bytes).fill(1), meta };
}

/** Store on the in-memory fake with a deterministic, incrementing clock. */
function trackedStore(maxBytes: number) {
	let clock = 0;
	const store = new PersistentFaceStore(createMemoryFaceStoreBackend(), {
		maxBytes,
		now: () => ++clock
	});
	return { store };
}

describe("atlasFaceStore", () => {
	it("returns undefined for an unknown hash", async () => {
		const { store } = trackedStore(1024);
		expect(await store.get("front:red:5:")).toBeUndefined();
	});

	it("round-trips a face with its pixels and meta payload", async () => {
		const { store } = trackedStore(1024);
		const meta = { key: { type: "red", value: "5", turned: false } };
		await store.put(record("front:red:5:", FACE_BYTES, meta));

		const row = await store.get("front:red:5:");
		expect(row).toBeDefined();
		expect(row?.width).toBe(43);
		expect(row?.height).toBe(60);
		expect(row?.data.byteLength).toBe(FACE_BYTES);
		expect(row?.meta).toEqual(meta);
	});

	it("evicts the least recently used face once a read reorders recency", async () => {
		const { store } = trackedStore(25);
		await store.put(record("a", 10)); // lastUsed 1
		await store.put(record("b", 10)); // lastUsed 2
		await store.get("a"); // touch -> lastUsed 3, so b is oldest now
		await store.put(record("c", 10)); // 30 bytes > 25 -> evict b

		expect(await store.get("b")).toBeUndefined();
		expect(await store.get("a")).toBeDefined();
		expect(await store.get("c")).toBeDefined();
	});

	it("keeps total bytes within the size cap", async () => {
		const { store } = trackedStore(20);
		await store.put(record("a", 10));
		await store.put(record("b", 10));
		await store.put(record("c", 10));

		const rows = await store.entries();
		const total = rows.reduce((sum, row) => sum + row.data.byteLength, 0);
		expect(total).toBeLessThanOrEqual(20);
		expect(rows.map((row) => row.hash)).toEqual(["b", "c"]);
	});

	it("keeps the newest face even when it alone exceeds the cap", async () => {
		const { store } = trackedStore(5);
		await store.put(record("big", 10));
		expect((await store.entries()).length).toBe(1);
	});

	it("prunes hashes outside the live set", async () => {
		const { store } = trackedStore(1024);
		await store.put(record("live", 10));
		await store.put(record("stale", 10));

		const removed = await store.prune(new Set(["live"]));
		expect(removed).toBe(1);
		expect((await store.entries()).map((row) => row.hash)).toEqual(["live"]);
	});

	it("serializes concurrent writes without losing records", async () => {
		const { store } = trackedStore(1024);
		await Promise.all(["a", "b", "c", "d", "e"].map((hash) => store.put(record(hash, 10))));
		expect((await store.entries()).length).toBe(5);
	});

	it("round-trips through the IndexedDB backend", async () => {
		// INFO: vitest runs without IndexedDB and fake-indexeddb is not a
		// dependency, so this exercises the backend's in-memory fallback.
		const backend = createIndexedDbFaceStoreBackend();
		await backend.write(record("x", 4, { k: 1 }));
		expect((await backend.read("x"))?.data.byteLength).toBe(4);
		await backend.remove("x");
		expect(await backend.read("x")).toBeUndefined();
	});
});
