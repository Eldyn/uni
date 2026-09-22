import { describe, it, expect } from "vitest";
import {
	preloadCardArt,
	getLayerTexture,
	getFaceTexture,
	type CardFaceKey
} from "$components/game/three/cardFaceAtlas";

const key: CardFaceKey = { type: "red", value: "5", turned: false };

describe("cardFaceAtlas: per-layer slots", () => {
	it("bakes background/value/border as distinct slots", async () => {
		await preloadCardArt();

		const background = getLayerTexture("background", key);
		const value = getLayerTexture("value", key);
		const border = getLayerTexture("border", key);
		const composite = getFaceTexture(key);

		for (const entry of [background, value, border]) {
			expect(entry.u1 - entry.u0).toBeGreaterThan(0);
			expect(entry.v1 - entry.v0).toBeGreaterThan(0);
		}

		// Each layer owns its own atlas slot, so the renderer can stack mod art
		// between the background and the value/border.
		expect(background.u0).not.toBe(value.u0);
		expect(value.u0).not.toBe(border.u0);
		expect(background.u0).not.toBe(composite.u0);
	});

	it("caches layer slots by key + layer", async () => {
		await preloadCardArt();
		expect(getLayerTexture("border", key)).toBe(getLayerTexture("border", key));
		expect(getLayerTexture("background", key)).not.toBe(getLayerTexture("border", key));
	});
});
