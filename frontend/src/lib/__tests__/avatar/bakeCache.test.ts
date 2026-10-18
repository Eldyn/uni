import { beforeEach, describe, expect, it } from "vitest";
import {
	PersistentFaceStore,
	createMemoryFaceStoreBackend
} from "$components/game/three/atlasFaceStore";
import { createBakeCache } from "$lib/avatar/bakeCache";
import { setCatalogueForTest } from "$lib/avatar/catalogue";
import { createImage, type PixelImage } from "$lib/avatar/pixels";
import { ART_VERSION, hashWithVersion, loadoutHash } from "$lib/avatar/slotRules";
import { FRAME_SIZE, STRIP_WIDTH } from "$lib/avatar/types";
import { avatarFixture, fullLoadout } from "./avatarFixture";

/** A fake image loader that records every requested asset path. */
function fakeLoader(): { loadImage: (path: string) => Promise<PixelImage>; calls: string[] } {
	const calls: string[] = [];
	return {
		calls,
		async loadImage(path) {
			calls.push(path);
			return createImage(STRIP_WIDTH, FRAME_SIZE);
		}
	};
}

function memoryStore(): PersistentFaceStore {
	return new PersistentFaceStore(createMemoryFaceStoreBackend());
}

describe("createBakeCache", () => {
	beforeEach(() => {
		setCatalogueForTest(avatarFixture());
	});

	it("get bakes once for equal loadouts", async () => {
		const loader = fakeLoader();
		const cache = createBakeCache({ loadImage: loader.loadImage, store: memoryStore() });

		const first = await cache.get(fullLoadout());
		const callsAfterFirst = loader.calls.length;
		const second = await cache.get(fullLoadout());

		expect(second).toBe(first);
		expect(loader.calls.length).toBe(callsAfterFirst);
	});

	it("concurrent gets share one bake", async () => {
		const loader = fakeLoader();
		const cache = createBakeCache({ loadImage: loader.loadImage, store: memoryStore() });

		const [first, second] = await Promise.all([cache.get(fullLoadout()), cache.get(fullLoadout())]);

		expect(second).toBe(first);
		expect(loader.calls.length).toBe(new Set(loader.calls).size);
	});

	it("peek is undefined before get and defined after", async () => {
		const loader = fakeLoader();
		const cache = createBakeCache({ loadImage: loader.loadImage, store: memoryStore() });

		expect(cache.peek(fullLoadout())).toBeUndefined();
		const strip = await cache.get(fullLoadout());

		expect(cache.peek(fullLoadout())).toBe(strip);
	});

	it("a persisted strip is reused after clear", async () => {
		const store = memoryStore();
		const loader = fakeLoader();
		const cache = createBakeCache({ loadImage: loader.loadImage, store });
		const baked = await cache.get(fullLoadout());

		cache.clear();
		expect(cache.peek(fullLoadout())).toBeUndefined();

		// A fresh cache with the same store must read the persisted strip
		// instead of loading the source art again.
		const reloadedLoader = fakeLoader();
		const reloaded = createBakeCache({ loadImage: reloadedLoader.loadImage, store });
		const strip = await reloaded.get(fullLoadout());

		expect(reloadedLoader.calls).toHaveLength(0);
		expect(strip.hash).toBe(loadoutHash(fullLoadout()));
		expect(Array.from(strip.image.data)).toEqual(Array.from(baked.image.data));
	});

	it("a different ART_VERSION misses the persisted strip", async () => {
		const loader = fakeLoader();
		const store = memoryStore();
		const loadout = fullLoadout();

		const stale = createImage(STRIP_WIDTH, FRAME_SIZE);
		stale.data.fill(9);
		await store.put({
			hash: hashWithVersion(loadout, ART_VERSION - 1),
			width: STRIP_WIDTH,
			height: FRAME_SIZE,
			data: new Uint8Array(stale.data)
		});

		const cache = createBakeCache({ loadImage: loader.loadImage, store });
		const strip = await cache.get(loadout);

		expect(strip.hash).toBe(loadoutHash(loadout));
		expect(loader.calls.length).toBeGreaterThan(0);
		expect(Array.from(strip.image.data)).not.toContain(9);
	});

	it("loadImage is called once per asset path across loadouts", async () => {
		const loader = fakeLoader();
		const cache = createBakeCache({ loadImage: loader.loadImage, store: memoryStore() });

		await cache.get(fullLoadout());
		await cache.get(
			fullLoadout({
				slots: { ...fullLoadout().slots, eyes: { item: "uni:eyes_wide", colours: [] } }
			})
		);

		const counts = new Map<string, number>();
		for (const path of loader.calls) counts.set(path, (counts.get(path) ?? 0) + 1);

		expect(loader.calls).toContain("eyes/uni_eyes_wide.png");
		expect([...counts.values()].every((count) => count === 1)).toBe(true);
	});

	it("a failing loadImage rejects get and does not poison the cache", async () => {
		let attempts = 0;
		const loadImage = async (): Promise<PixelImage> => {
			attempts++;
			if (attempts === 1) throw new Error("decode failed");
			return createImage(STRIP_WIDTH, FRAME_SIZE);
		};
		const cache = createBakeCache({ loadImage, store: memoryStore() });

		await expect(cache.get(fullLoadout())).rejects.toThrow("decode failed");
		expect(cache.peek(fullLoadout())).toBeUndefined();

		const strip = await cache.get(fullLoadout());

		expect(attempts).toBeGreaterThan(1);
		expect(strip.image.width).toBe(STRIP_WIDTH);
	});
});
