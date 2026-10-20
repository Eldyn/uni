import { describe, expect, it } from "vitest";
import { cardBackUrl, cardFaceUrl } from "$lib/cards/skinPaths";

describe("skinPaths", () => {
	it("uses the base face when no skin is equipped", () => {
		expect(cardFaceUrl("7", null)).toBe("/assets/cards/7.png");
	});

	it("uses the base face for an unknown skin", () => {
		expect(cardFaceUrl("7", "uni:not_a_card")).toBe("/assets/cards/7.png");
	});

	it("uses the catalogue folder for an equipped face skin", () => {
		expect(cardFaceUrl("7", "uni:card_face_midnight")).toBe("/assets/cards/skins/midnight/7.png");
	});

	it("uses the base back when no skin is equipped", () => {
		expect(cardBackUrl(null)).toBe("/assets/cards/back.png");
	});

	it("uses the catalogue asset for an equipped back skin", () => {
		expect(cardBackUrl("uni:card_back_stars")).toBe("/assets/cards/backs/stars.png");
	});
});
