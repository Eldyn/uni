import { describe, it, expect, vi } from "vitest";
import {
	faceKeyHash,
	getFaceTexture,
	getAtlasPage,
	atlasPageCount,
	ATLAS_PAGE_VERSION,
	preloadCardArt,
	cardFaceKeyFor,
	hasStandardFaceArt,
	standardArtUrls,
	loadFirstArtSource,
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

	it("handles concurrent preloadCardArt calls safely via singleton promise cache", async () => {
		const [p1, p2, p3] = [preloadCardArt(), preloadCardArt(), preloadCardArt()];
		expect(p1).toBeInstanceOf(Promise);
		expect(p2).toBeInstanceOf(Promise);
		expect(p3).toBeInstanceOf(Promise);
		await Promise.all([p1, p2, p3]);
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
		const paintedJollyKey: CardFaceKey = {
			type: "white",
			value: "jolly",
			wildColor: "green",
			turned: false
		};
		const paintedJollyEntry = getFaceTexture(paintedJollyKey);
		expect(paintedJollyEntry).toBeDefined();
	});
});

describe("cardFaceKeyFor defs-face resolution", () => {
	it("recognizes baked default art labels", () => {
		expect(hasStandardFaceArt("5")).toBe(true);
		expect(hasStandardFaceArt("skip")).toBe(true);
		expect(hasStandardFaceArt("jolly_draw4")).toBe(true);
		expect(hasStandardFaceArt("banana")).toBe(false);
		expect(hasStandardFaceArt(undefined)).toBe(false);
	});

	it("maps a default text face onto the same color/value art plus art_version", () => {
		const key = cardFaceKeyFor(
			{
				type: "red",
				value: "5",
				face: { kind: "text", color: "red", label: "5", art_version: 1 }
			},
			undefined,
			false
		);
		expect(key).toEqual({
			type: "red",
			value: "5",
			wildColor: undefined,
			turned: false,
			artVersion: "1"
		});
	});

	it("carries a wildColor through while keeping the face art version", () => {
		const key = cardFaceKeyFor(
			{
				type: "white",
				value: "jolly",
				face: { kind: "text", color: "white", label: "jolly", art_version: 2 }
			},
			"green",
			false
		);
		expect(key.type).toBe("white");
		expect(key.value).toBe("jolly");
		expect(key.wildColor).toBe("green");
		expect(key.artVersion).toBe("2");
	});

	it("falls back to the card's own type/value when a face has no baked art", () => {
		const key = cardFaceKeyFor(
			{
				type: "white",
				value: "banana",
				face: { kind: "emoji", label: "banana", art_version: 3 }
			},
			undefined,
			false
		);
		expect(key).toEqual({ type: "white", value: "banana", wildColor: undefined, turned: false });
		expect(key.artVersion).toBeUndefined();
	});

	it("falls back cleanly when the card carries no face at all", () => {
		const key = cardFaceKeyFor({ type: "blue", value: "7" }, undefined, false);
		expect(key).toEqual({ type: "blue", value: "7", wildColor: undefined, turned: false });
		expect(key.artVersion).toBeUndefined();
	});

	it("uses the card back for turned cards regardless of face", () => {
		const key = cardFaceKeyFor(
			{
				type: "red",
				value: "5",
				face: { kind: "text", color: "red", label: "5", art_version: 1 }
			},
			"blue",
			true
		);
		expect(key).toEqual({ type: "wild", value: "0", turned: true });
	});
});

describe("cardFaceAtlas equipped skins", () => {
	const FACE = "uni:card_face_midnight";
	const BACK = "uni:card_back_stars";

	it("leaves the default hashes unchanged", () => {
		expect(faceKeyHash({ type: "red", value: "5", turned: false })).toBe("front:red:5::");
		expect(faceKeyHash({ type: "wild", value: "0", turned: true })).toBe("back:");
	});

	it("hashes a face skin into the front key and re-bakes its slot", () => {
		const plain: CardFaceKey = { type: "red", value: "5", turned: false };
		const skinned: CardFaceKey = { ...plain, skinId: FACE };

		expect(faceKeyHash(skinned)).not.toBe(faceKeyHash(plain));
		expect(getFaceTexture(skinned)).not.toBe(getFaceTexture(plain));
		expect(getFaceTexture(skinned)).toBe(getFaceTexture({ ...plain, skinId: FACE }));
	});

	it("hashes a back skin into the turned key and re-bakes its slot", () => {
		const plain: CardFaceKey = { type: "wild", value: "0", turned: true };
		const skinned: CardFaceKey = { ...plain, backId: BACK };

		expect(faceKeyHash(skinned)).not.toBe(faceKeyHash(plain));
		expect(getFaceTexture(skinned)).not.toBe(getFaceTexture(plain));
		// A face skin is a front-only concern.
		expect(faceKeyHash({ ...plain, skinId: FACE })).toBe(faceKeyHash(plain));
	});

	it("carries the equipped ids through cardFaceKeyFor", () => {
		const card = {
			type: "red",
			value: "5",
			face: { kind: "text", color: "red", label: "5", art_version: 1 }
		};

		const plain = cardFaceKeyFor(card, undefined, false);
		expect(plain.skinId).toBeUndefined();
		expect(plain.backId).toBeUndefined();

		const front = cardFaceKeyFor(card, undefined, false, FACE, BACK);
		expect(front.skinId).toBe(FACE);
		expect(faceKeyHash(front)).not.toBe(faceKeyHash(plain));

		const back = cardFaceKeyFor(card, undefined, true, FACE, BACK);
		expect(back.backId).toBe(BACK);
		expect(faceKeyHash(back)).not.toBe(faceKeyHash(cardFaceKeyFor(card, undefined, true)));
	});

	it("requests the skinned art with a per-name base fallback", () => {
		const byName = new Map(standardArtUrls(FACE, BACK).map(({ name, urls }) => [name, urls]));

		expect(byName.get("5")).toEqual(["/assets/cards/skins/midnight/5.png", "/assets/cards/5.png"]);
		expect(byName.get("jolly_draw4")).toEqual([
			"/assets/cards/skins/midnight/jolly_draw4.png",
			"/assets/cards/jolly_draw4.png"
		]);
		expect(byName.get("back")).toEqual(["/assets/cards/backs/stars.png", "/assets/cards/back.png"]);
	});

	it("leaves preload on the base art with no skin", () => {
		const sources = standardArtUrls();
		expect(sources.every(({ urls }) => urls.length === 1)).toBe(true);
		expect(sources.find(({ name }) => name === "back")?.urls).toEqual(["/assets/cards/back.png"]);
		expect(sources.find(({ name }) => name === "5")?.urls).toEqual(["/assets/cards/5.png"]);
	});

	it("falls back to the next source when a skinned file is missing", async () => {
		const requested: string[] = [];
		const image = {} as CanvasImageSource;
		const load = vi.fn(async (url: string) => {
			requested.push(url);
			if (url.includes("/skins/")) throw new Error("404");
			return image;
		});

		const loaded = await loadFirstArtSource(
			["/assets/cards/skins/midnight/5.png", "/assets/cards/5.png"],
			load
		);

		expect(loaded).toBe(image);
		expect(requested).toEqual(["/assets/cards/skins/midnight/5.png", "/assets/cards/5.png"]);
	});

	it("preloads the skinned variant end to end", async () => {
		await preloadCardArt(FACE, BACK);
		const skinned: CardFaceKey = { type: "red", value: "5", turned: false, skinId: FACE };
		expect(getFaceTexture(skinned)).toBeDefined();
	});
});
