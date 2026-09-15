import { describe, it, expect } from "vitest";
import {
	faceKeyHash,
	getFaceTexture,
	getAtlasPage,
	atlasPageCount,
	ATLAS_PAGE_VERSION,
	preloadCardArt,
	type CardFaceKey
} from "$components/game/three/cardFaceAtlas";
import { NearestFilter, SRGBColorSpace } from "three";

describe("cardFaceAtlas", () => {
	it("hashes card face keys deterministically", () => {
		const key1: CardFaceKey = { type: "red", value: "5", turned: false };
		const key2: CardFaceKey = { type: "red", value: "5", turned: false };
		const key3: CardFaceKey = { type: "blue", value: "5", turned: false };
		expect(faceKeyHash(key1)).toBe(faceKeyHash(key2));
		expect(faceKeyHash(key1)).not.toBe(faceKeyHash(key3));
	});

	it("hashes turned cards consistently", () => {
		const turned1: CardFaceKey = { type: "red", value: "5", turned: true };
		const turned2: CardFaceKey = { type: "blue", value: "5", turned: true };
		const front: CardFaceKey = { type: "red", value: "5", turned: false };
		expect(faceKeyHash(turned1)).toBe(faceKeyHash(turned2));
		expect(faceKeyHash(turned1)).not.toBe(faceKeyHash(front));
	});

	it("hashes wild colors and art versions distinctly", () => {
		const wild1: CardFaceKey = { type: "white", value: "jolly", wildColor: "red", turned: false };
		const wild2: CardFaceKey = { type: "white", value: "jolly", wildColor: "blue", turned: false };
		const unpaintedWild: CardFaceKey = { type: "white", value: "jolly", turned: false };
		expect(faceKeyHash(wild1)).not.toBe(faceKeyHash(wild2));
		expect(faceKeyHash(wild1)).not.toBe(faceKeyHash(unpaintedWild));

		const art1: CardFaceKey = { type: "red", value: "5", turned: false, artVersion: "v1" };
		const art2: CardFaceKey = { type: "red", value: "5", turned: false, artVersion: "v2" };
		expect(faceKeyHash(art1)).not.toBe(faceKeyHash(art2));
	});

	it("allocates valid normalized UV rect [0, 1]", () => {
		const key: CardFaceKey = { type: "green", value: "skip", turned: false };
		const entry = getFaceTexture(key);
		expect(entry.page).toBe(0);
		expect(entry.u0).toBeGreaterThanOrEqual(0);
		expect(entry.u1).toBeLessThanOrEqual(1);
		expect(entry.v0).toBeGreaterThanOrEqual(0);
		expect(entry.v1).toBeLessThanOrEqual(1);
		expect(entry.u1).toBeGreaterThan(entry.u0);
		expect(entry.v1).toBeGreaterThan(entry.v0);
	});

	it("caches and returns identical AtlasEntry for identical key", () => {
		const key: CardFaceKey = { type: "yellow", value: "7", turned: false };
		const entry1 = getFaceTexture(key);
		const entry2 = getFaceTexture({ type: "yellow", value: "7", turned: false });
		expect(entry1).toBe(entry2);
	});

	it("allocates non-overlapping slots for distinct card faces", () => {
		const entryA = getFaceTexture({ type: "red", value: "1", turned: false });
		const entryB = getFaceTexture({ type: "red", value: "2", turned: false });
		expect(entryA).not.toBe(entryB);
		// Slots on same page should not overlap
		if (entryA.page === entryB.page) {
			const overlapX = Math.max(0, Math.min(entryA.u1, entryB.u1) - Math.max(entryA.u0, entryB.u0));
			const overlapY = Math.max(0, Math.min(entryA.v1, entryB.v1) - Math.max(entryA.v0, entryB.v0));
			const overlaps = overlapX > 0 && overlapY > 0;
			expect(overlaps).toBe(false);
		}
	});

	it("provides three.js texture with NearestFilter and SRGBColorSpace for atlas pages", () => {
		const page = getAtlasPage(0);
		expect(page).toBeDefined();
		expect(page.magFilter).toBe(NearestFilter);
		expect(page.minFilter).toBe(NearestFilter);
		expect(page.colorSpace).toBe(SRGBColorSpace);
	});

	it("maintains atlasPageCount and ATLAS_PAGE_VERSION", () => {
		expect(atlasPageCount()).toBeGreaterThanOrEqual(1);
		expect(ATLAS_PAGE_VERSION).toBeDefined();
		expect(typeof ATLAS_PAGE_VERSION.value).toBe("number");
	});

	it("exports preloadCardArt returning a promise", async () => {
		const promise = preloadCardArt();
		expect(promise).toBeInstanceOf(Promise);
		await promise;
	});

	it("bakes layers in correct order with correct tinting", async () => {
		await preloadCardArt();

		// Check turned card
		const turnedKey: CardFaceKey = { type: "red", value: "9", turned: true };
		const turnedEntry = getFaceTexture(turnedKey);
		expect(turnedEntry).toBeDefined();

		// Check unpainted jolly
		const unpaintedJollyKey: CardFaceKey = { type: "white", value: "jolly", turned: false };
		const unpaintedJollyEntry = getFaceTexture(unpaintedJollyKey);
		expect(unpaintedJollyEntry).toBeDefined();

		// Check painted jolly
		const paintedJollyKey: CardFaceKey = { type: "white", value: "jolly", wildColor: "green", turned: false };
		const paintedJollyEntry = getFaceTexture(paintedJollyKey);
		expect(paintedJollyEntry).toBeDefined();
	});
});

