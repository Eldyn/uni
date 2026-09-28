import { describe, it, expect } from "vitest";
import {
	computeSeatLabelLayout,
	estimateTextUnits,
	formatSeatName,
	layoutArcGlyphs,
	seatOverheadRadiusPx,
	seatWorldPerPx
} from "$components/game/three/seatLabel";

describe("formatSeatName", () => {
	it("passes through short names and handles empty input", () => {
		expect(formatSeatName("bob")).toBe("bob");
		expect(formatSeatName("")).toBe("");
		expect(formatSeatName(undefined)).toBe("");
	});

	it("truncates names longer than 16 chars with an ellipsis", () => {
		expect(formatSeatName("a_very_extremely_long_name_indeed")).toBe("a_very_extremel…");
	});
});

describe("estimateTextUnits", () => {
	it("weights wide glyphs above narrow ones and never returns zero", () => {
		expect(estimateTextUnits("mmm")).toBeGreaterThan(estimateTextUnits("iii"));
		expect(estimateTextUnits("")).toBeGreaterThanOrEqual(1);
	});
});

describe("computeSeatLabelLayout", () => {
	const overheadRadius = 52;

	it("uses the (halved) base em font for short names", () => {
		const layout = computeSeatLabelLayout("bob", overheadRadius, 1.15);
		expect(layout.baseFontSize).toBe(10);
		expect(layout.fontSize).toBe(10);
		expect(layout.dy).toBe(-4);
		expect(layout.needsCompression).toBe(false);
	});

	it("shrinks the font as names get longer", () => {
		const tightRadius = 22;
		const short = computeSeatLabelLayout("bob", tightRadius, 1.15);
		const medium = computeSeatLabelLayout("scriptxcorso", tightRadius, 1.15);
		const long = computeSeatLabelLayout("superlongplayer", tightRadius, 1.15);

		expect(medium.fontSize).toBeLessThan(short.fontSize);
		expect(medium.fontSize).toBeGreaterThanOrEqual(5);
		expect(long.fontSize).toBeLessThan(medium.fontSize);
		expect(long.fontSize).toBeGreaterThanOrEqual(5);
		expect(long.dy).toBeLessThanOrEqual(-2);
	});

	it("falls back to the minimum size and flags compression for wide scripts", () => {
		const layout = computeSeatLabelLayout("日本語日本語日本語日本語日本語日本語", 20, 1.15);
		expect(layout.fontSize).toBe(5);
		expect(layout.needsCompression).toBe(true);
	});
});

describe("layoutArcGlyphs", () => {
	it("returns nothing for an empty glyph list", () => {
		expect(layoutArcGlyphs([], 100, 0)).toEqual([]);
	});

	it("centres equal-width glyphs symmetrically on the top of the arc", () => {
		const glyphs = [
			{ char: "a", width: 10 },
			{ char: "b", width: 10 },
			{ char: "c", width: 10 }
		];
		const placed = layoutArcGlyphs(glyphs, 100, 0);

		expect(placed.map((g) => g.char)).toEqual(["a", "b", "c"]);
		expect(placed[0].x).toBeCloseTo(-placed[2].x, 5);
		expect(placed[1].x).toBeCloseTo(0, 5);
		expect(placed[1].y).toBeCloseTo(-100, 5);
		expect(placed[1].rotationRad).toBeCloseTo(0, 5);
	});

	it("compresses advances (and the arc span) when maxTextLength is exceeded", () => {
		const glyphs = [
			{ char: "a", width: 10 },
			{ char: "b", width: 10 },
			{ char: "c", width: 10 }
		];
		const wide = layoutArcGlyphs(glyphs, 100, 0);
		const compressed = layoutArcGlyphs(glyphs, 100, 15);

		expect(Math.abs(compressed[0].x)).toBeLessThan(Math.abs(wide[0].x));
		expect(Math.abs(compressed[2].x)).toBeLessThan(Math.abs(wide[2].x));
	});
});

describe("seatOverheadRadiusPx", () => {
	it("scales the rail purely with the avatar box, with no px floor", () => {
		// Mobile avatar boxes run ~20-30px; a floor at 40 (the old behaviour)
		// made the label's world size diverge from the avatar's on those sizes.
		expect(seatOverheadRadiusPx(20)).toBe(18);
		expect(seatOverheadRadiusPx(56)).toBe(52);
		expect(seatOverheadRadiusPx(112)).toBe(103);
		// Doubling the avatar box roughly doubles the rail (rounding aside), so
		// the label's world size (canvas × avatarWorld/avatarPx) stays constant.
		expect(seatOverheadRadiusPx(112)).toBeGreaterThan(seatOverheadRadiusPx(56) * 1.9);
	});
});

describe("seatWorldPerPx", () => {
	it("keeps the label proportional to the avatar's world size", () => {
		const largeAvatar = seatWorldPerPx(0.78, 56);
		const smallAvatar = seatWorldPerPx(0.78, 112);
		expect(smallAvatar).toBeCloseTo(largeAvatar / 2, 6);
		expect(seatWorldPerPx(0.78, 0)).toBeGreaterThan(0);
	});
});
