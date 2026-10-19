import { describe, expect, it } from "vitest";
import { defaultPaletteId, isPaletteLocked, paletteOfSwatch } from "$lib/avatar/palettes";
import type { Palette } from "$lib/avatar/types";

const BASIC: Palette = {
	id: "basic",
	name: "Basic",
	price: 0,
	starter: true,
	swatches: [{ id: "basic:red", hex: "#e84a4a" }]
};
const NEON: Palette = {
	id: "uni:neon",
	name: "Neon",
	price: 250,
	swatches: [
		{ id: "uni:neon:hot", hex: "#ff2d95" },
		{ id: "uni:neon:volt", hex: "#00e5ff" }
	]
};
const ALL = [NEON, BASIC];

describe("palettes", () => {
	it("finds the palette of a swatch", () => {
		expect(paletteOfSwatch(ALL, "uni:neon:volt")?.id).toBe("uni:neon");
		expect(paletteOfSwatch(ALL, "nope")).toBeUndefined();
		expect(paletteOfSwatch(ALL, undefined)).toBeUndefined();
	});
	it("locks a palette only when every swatch is locked", () => {
		expect(isPaletteLocked(NEON, new Set(["uni:neon:hot"]))).toBe(false);
		expect(isPaletteLocked(NEON, new Set(["uni:neon:hot", "uni:neon:volt"]))).toBe(true);
	});
	it("defaults to the swatch palette, else the first palette not locked", () => {
		const locked = new Set(["uni:neon:hot", "uni:neon:volt"]);
		expect(defaultPaletteId(ALL, "uni:neon:hot", locked)).toBe("uni:neon");
		expect(defaultPaletteId(ALL, "", locked)).toBe("basic");
	});
});
