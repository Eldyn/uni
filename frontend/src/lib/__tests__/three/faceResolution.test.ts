import { describe, it, expect } from "vitest";
import {
	resolveFace,
	tiersUpTo,
	defaultKeep,
	type ResolvedFace
} from "$components/game/three/faceResolution";
import { AssetTextureCache } from "$components/game/three/assetTextures";
import type { Texture } from "three";

const imageFace = (slots: ResolvedFace["slots"]): ResolvedFace => ({
	kind: "image",
	art: "bomb",
	art_mode: "inset",
	slots
});

describe("faceResolution: tier ceiling", () => {
	it("orders tiers richest first from the ceiling", () => {
		expect(tiersUpTo("high")).toEqual(["high", "medium", "low"]);
		expect(tiersUpTo("medium")).toEqual(["medium", "low"]);
		expect(tiersUpTo("low")).toEqual(["low"]);
	});

	it("picks the richest variant at or below the ceiling", () => {
		const face = imageFace({
			art: [
				{ tier: "high", url: "/assets/a", hash: "aa" },
				{ tier: "low", url: "/assets/b", hash: "bb" }
			]
		});
		expect(resolveFace(face, "high").art).toEqual({
			source: "asset",
			url: "/assets/a",
			hash: "aa"
		});
		expect(resolveFace(face, "low").art).toEqual({
			source: "asset",
			url: "/assets/b",
			hash: "bb"
		});
	});

	it("falls down the tier chain when the richest is absent", () => {
		const face = imageFace({ art: [{ tier: "medium", url: "/assets/m", hash: "mm" }] });
		expect(resolveFace(face, "high").art).toEqual({
			source: "asset",
			url: "/assets/m",
			hash: "mm"
		});
	});
});

describe("faceResolution: fallback chain always terminates", () => {
	it("image: art -> bundle emoji -> face glyph -> procedural", () => {
		expect(
			resolveFace(imageFace({ art: [{ tier: "high", url: "u", hash: "h" }] }), "high").fallback
		).toBe("asset");
		expect(resolveFace(imageFace({ emoji: [{ tier: "low", value: "💣" }] }), "high").fallback).toBe(
			"bundle-emoji"
		);
		expect(resolveFace({ kind: "image", label: "X" }, "high").fallback).toBe("face-emoji");
		expect(resolveFace({ kind: "image" }, "high").fallback).toBe("procedural");
	});

	it("emoji: bundle emoji -> face glyph -> blank", () => {
		expect(
			resolveFace({ kind: "emoji", slots: { emoji: [{ tier: "low", value: "🃏" }] } }, "high")
				.fallback
		).toBe("bundle-emoji");
		expect(resolveFace({ kind: "emoji", label: "★" }, "high").fallback).toBe("face-emoji");
		expect(resolveFace({ kind: "emoji" }, "high").fallback).toBe("blank");
	});

	it("text uses the procedural atlas; blank stays blank", () => {
		const text = resolveFace({ kind: "text", color: "red", label: "5" }, "high");
		expect(text.procedural).toBe(true);
		expect(text.art).toBeNull();
		const blank = resolveFace({ kind: "blank" }, "high");
		expect(blank.fallback).toBe("blank");
		expect(blank.procedural).toBe(false);
	});

	it("an undefined face resolves to blank", () => {
		expect(resolveFace(undefined, "high").fallback).toBe("blank");
	});
});

describe("faceResolution: composition defaults", () => {
	it("keeps value+border except in replace mode", () => {
		expect(defaultKeep("inset")).toEqual(["value", "border"]);
		expect(defaultKeep("overlay")).toEqual(["value", "border"]);
		expect(defaultKeep("replace")).toEqual([]);
	});

	it("carries art_mode/art_fit/keep through the plan", () => {
		const plan = resolveFace(
			{ kind: "image", art_mode: "replace", art_fit: "cover", keep: ["border"] },
			"high"
		);
		expect(plan.art_mode).toBe("replace");
		expect(plan.art_fit).toBe("cover");
		expect(plan.keep).toEqual(["border"]);
	});
});

describe("assetTextures: cache keying", () => {
	const fakeTexture = () => ({ colorSpace: "", dispose() {} }) as unknown as Texture;

	it("returns one texture per content hash", () => {
		let loads = 0;
		const cache = new AssetTextureCache(
			{
				load() {
					loads += 1;
					return fakeTexture();
				}
			},
			() => fakeTexture()
		);

		const a = cache.getImage("hash-a", "/assets/a");
		const b = cache.getImage("hash-a", "/assets/a");
		expect(a).toBe(b);
		expect(loads).toBe(1);
		cache.getImage("hash-b", "/assets/b");
		expect(loads).toBe(2);
		expect(cache.imageCount).toBe(2);
	});

	it("returns one texture per glyph", () => {
		let renders = 0;
		const cache = new AssetTextureCache({ load: fakeTexture }, () => {
			renders += 1;
			return fakeTexture();
		});
		expect(cache.getGlyph("💣")).toBe(cache.getGlyph("💣"));
		expect(renders).toBe(1);
		expect(cache.glyphCount).toBe(1);
	});

	it("clears both caches", () => {
		const cache = new AssetTextureCache({ load: fakeTexture }, () => fakeTexture());
		cache.getImage("h", "u");
		cache.getGlyph("x");
		cache.clear();
		expect(cache.imageCount).toBe(0);
		expect(cache.glyphCount).toBe(0);
	});
});
