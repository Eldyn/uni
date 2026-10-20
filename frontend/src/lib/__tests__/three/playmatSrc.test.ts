import { describe, expect, it } from "vitest";
import { playmatUrls } from "$components/game/three/playmatSrc";

const BASE = { desktop: "/assets/playmat.png", mobile: "/assets/mobile_playmat.png" };

describe("playmatUrls", () => {
	it("uses the base sheets with no skin equipped", () => {
		expect(playmatUrls(null)).toEqual(BASE);
	});

	it("uses the base sheets for an unknown item", () => {
		expect(playmatUrls("uni:not_a_playmat")).toEqual(BASE);
	});

	it("uses the base sheets for an item from another slot", () => {
		expect(playmatUrls("uni:card_back_stars")).toEqual(BASE);
	});

	it("derives the mobile sheet from the equipped playmat's desktop sheet", () => {
		expect(playmatUrls("uni:playmat_dusk")).toEqual({
			desktop: "/assets/playmat/dusk.png",
			mobile: "/assets/playmat/dusk_mobile.png"
		});
	});
});
