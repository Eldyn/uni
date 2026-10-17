import { describe, it, expect, beforeAll, afterAll, beforeEach, vi } from "vitest";
import {
	PersistentFaceStore,
	createMemoryFaceStoreBackend
} from "$components/game/three/atlasFaceStore";

type AtlasModule = typeof import("$components/game/three/cardFaceAtlas");
type CardFaceKey = AtlasModule["CardFaceKey"];

const CARD_W = 43;
const CARD_H = 60;

interface PaintCall {
	data: Uint8ClampedArray;
	width: number;
	height: number;
	x: number;
	y: number;
}

const putImageDataCalls: PaintCall[] = [];
let extractCount = 0;
const ctxByCanvas = new WeakMap<HTMLCanvasElement, CanvasRenderingContext2D>();

/**
 * Stands in for the 2D context jsdom refuses to hand out: drawing is a
 * no-op, getImageData hands back fabricated pixels, and putImageData is
 * recorded so tests can prove hydration painted persisted bytes.
 */
function createFakeContext(canvas: HTMLCanvasElement): CanvasRenderingContext2D {
	const ctx = {
		canvas,
		globalCompositeOperation: "source-over",
		fillStyle: "#000000",
		clearRect: () => {},
		fillRect: () => {},
		drawImage: () => {},
		getImageData: (x: number, y: number, width: number, height: number) => {
			extractCount++;
			return {
				data: new Uint8ClampedArray(width * height * 4).fill(123),
				width,
				height
			};
		},
		putImageData: (
			imgData: { data: Uint8ClampedArray; width: number; height: number },
			x: number,
			y: number
		) => {
			putImageDataCalls.push({
				data: imgData.data,
				width: imgData.width,
				height: imgData.height,
				x,
				y
			});
		}
	};
	return ctx as unknown as CanvasRenderingContext2D;
}

let atlas: AtlasModule;

beforeAll(async () => {
	// INFO: must run before the atlas import, because ensurePage(0) captures
	// its canvas context at module init.
	const fakeGetContext = function (this: HTMLCanvasElement) {
		let ctx = ctxByCanvas.get(this);
		if (!ctx) {
			ctx = createFakeContext(this);
			ctxByCanvas.set(this, ctx);
		}
		return ctx;
	} as unknown as HTMLCanvasElement["getContext"];
	vi.spyOn(HTMLCanvasElement.prototype, "getContext").mockImplementation(fakeGetContext);
	// INFO: jsdom ships no ImageData; hydration needs the constructor to
	// turn stored bytes into a paintable image.
	if (typeof globalThis.ImageData === "undefined") {
		vi.stubGlobal(
			"ImageData",
			class ImageDataStub {
				data: Uint8ClampedArray;
				width: number;
				height: number;
				constructor(data: Uint8ClampedArray, width: number, height: number) {
					this.data = data;
					this.width = width;
					this.height = height;
				}
			}
		);
	}
	atlas = await import("$components/game/three/cardFaceAtlas");
});

afterAll(() => {
	vi.restoreAllMocks();
	vi.unstubAllGlobals();
});

/** Injects a fresh in-memory-backed store and restarts hydration. */
function freshStore(): PersistentFaceStore {
	const store = new PersistentFaceStore(createMemoryFaceStoreBackend());
	atlas.setAtlasFaceStore(store);
	return store;
}

function flushed(): Promise<void> {
	return new Promise((resolve) => setTimeout(resolve, 0));
}

describe("cardFaceAtlas persistence", () => {
	beforeEach(() => {
		putImageDataCalls.length = 0;
		extractCount = 0;
	});

	it("persists a deferred bake once art loads", async () => {
		const store = freshStore();
		const key: CardFaceKey = { type: "yellow", value: "4", turned: false };

		atlas.getFaceTexture(key); // art not loaded yet -> slot defers
		await flushed();
		expect((await store.entries()).length).toBe(0);

		await atlas.preloadCardArt();

		await vi.waitFor(async () => {
			const hashes = (await store.entries()).map((row) => row.hash);
			expect(hashes).toContain(atlas.faceKeyHash(key));
		});
		const row = (await store.entries()).find((r) => r.hash === atlas.faceKeyHash(key));
		expect(row?.width).toBe(CARD_W);
		expect(row?.height).toBe(CARD_H);
		expect(row?.data.byteLength).toBe(CARD_W * CARD_H * 4);
		expect(row?.meta).toEqual({ key });
	});

	it("persists a fresh bake keyed by the content hash", async () => {
		const store = freshStore();
		const key: CardFaceKey = { type: "blue", value: "8", turned: false };
		await atlas.preloadCardArt();

		atlas.getFaceTexture(key); // art is loaded -> bake runs immediately

		await vi.waitFor(async () => {
			const hashes = (await store.entries()).map((row) => row.hash);
			expect(hashes).toContain(atlas.faceKeyHash(key));
		});
		const row = (await store.entries()).find((r) => r.hash === atlas.faceKeyHash(key));
		expect(row?.meta).toEqual({ key });
		expect(row?.data.byteLength).toBe(CARD_W * CARD_H * 4);
	});

	it("hydrates persisted pixels into the in-memory bake cache", async () => {
		const store = freshStore();
		const key: CardFaceKey = { type: "green", value: "2", turned: false, artVersion: "9" };
		const hash = atlas.faceKeyHash(key);
		await store.put({
			hash,
			width: CARD_W,
			height: CARD_H,
			data: new Uint8Array(CARD_W * CARD_H * 4).fill(7),
			meta: { key }
		});

		await atlas.preloadCardArt(); // hydrates the record above into the cache

		const painted = putImageDataCalls.find((call) => call.data[0] === 7);
		expect(painted).toBeDefined();
		expect(painted?.width).toBe(CARD_W);
		expect(painted?.height).toBe(CARD_H);

		// A hydrated hash is served from the cache: no re-bake, so no
		// getImageData extraction happens for it.
		const before = extractCount;
		atlas.getFaceTexture(key);
		expect(extractCount).toBe(before);
	});

	it("prunes stale hashes and keeps a live hash's layer variants", async () => {
		const store = freshStore();
		const liveBase = "front:red:5:";
		const layerHash = `${liveBase}:border`;
		await store.put({ hash: liveBase, width: CARD_W, height: CARD_H, data: new Uint8Array(4) });
		await store.put({ hash: layerHash, width: CARD_W, height: CARD_H, data: new Uint8Array(4) });
		await store.put({
			hash: "front:red:9:",
			width: CARD_W,
			height: CARD_H,
			data: new Uint8Array(4)
		});

		const removed = await atlas.pruneStaleAtlasFaces([liveBase]);

		expect(removed).toBe(1);
		const hashes = (await store.entries()).map((row) => row.hash);
		expect(hashes).toContain(liveBase);
		expect(hashes).toContain(layerHash);
		expect(hashes).not.toContain("front:red:9:");
	});
});
