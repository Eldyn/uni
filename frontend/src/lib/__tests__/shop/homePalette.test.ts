import { describe, it, expect } from "vitest";
import {
	HOME_BACKGROUND_BASE_HEX,
	HOME_BACKGROUND_HINT_HEX,
	hexToCss,
	hexToRgb01
} from "$lib/shaders/homePalette";
import rippleQuadSource from "../../components/home/RippleQuad.svelte?raw";

describe("homePalette", () => {
	it("holds the home screen colors", () => {
		expect(HOME_BACKGROUND_BASE_HEX).toBe(0x16171d);
		expect(HOME_BACKGROUND_HINT_HEX).toBe(0x2c354d);
	});

	it("converts a hex number to 0..1 components", () => {
		expect(hexToRgb01(0xff8000)).toEqual([1, 128 / 255, 0]);
	});

	it("converts a hex number to a CSS string with six digits", () => {
		expect(hexToCss(0x16171d)).toBe("#16171d");
		expect(hexToCss(0x00000f)).toBe("#00000f");
	});

	it("is the only place that spells the home colors in RippleQuad", () => {
		expect(rippleQuadSource).toContain("HOME_BACKGROUND_BASE_HEX");
		expect(rippleQuadSource).toContain("HOME_BACKGROUND_HINT_HEX");
		expect(rippleQuadSource).not.toMatch(/hexToVec3\(0x[0-9a-fA-F]{6}\)/);
	});
});
