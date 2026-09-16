import { describe, it, expect } from "vitest";

import {
	computeHandLine,
	centerSlotIndex,
	CARD_LINE_SPACING_EM,
	CARD_LINE_MIN_SPACING_EM
} from "$components/game/layout/handLine";

describe("computeHandLine", () => {
	it("returns no slots for zero or negative cards", () => {
		expect(computeHandLine(0).slots).toEqual([]);
		expect(computeHandLine(-2).slots).toEqual([]);
	});

	it("centers a single card at x=0 with no rotation", () => {
		const [slot] = computeHandLine(1).slots;
		expect(slot.x).toBe(0);
		expect(slot.rotateDeg).toBe(0);
	});

	it("spaces adjacent cards by CARD_LINE_SPACING_EM and centers the row", () => {
		const { slots } = computeHandLine(5);
		expect(slots[1].x - slots[0].x).toBeCloseTo(CARD_LINE_SPACING_EM, 5);
		const sum = slots.reduce((total, slot) => total + slot.x, 0);
		expect(sum).toBeCloseTo(0, 5);
	});

	it("compresses spacing once the row would outgrow maxHalfSpanEm", () => {
		const maxHalfSpan = 10;
		expect(computeHandLine(5, maxHalfSpan).spacingEm).toBeCloseTo(CARD_LINE_SPACING_EM, 5);

		const crowded = computeHandLine(7, maxHalfSpan);
		expect(crowded.slots[0].x).toBeCloseTo(-maxHalfSpan, 5);
		expect(crowded.slots[crowded.slots.length - 1].x).toBeCloseTo(maxHalfSpan, 5);
		expect(crowded.spacingEm).toBeLessThan(CARD_LINE_SPACING_EM);
		expect(crowded.maxScrollEm).toBe(0);
	});

	// Past this point a card is a sliver with no readable value, which is
	// unusable on a touch screen — the row scrolls instead of packing tighter.
	it("stops compressing at the minimum spacing and turns the rest into scroll range", () => {
		const maxHalfSpan = 10;
		const overflowing = computeHandLine(20, maxHalfSpan);
		expect(overflowing.spacingEm).toBeCloseTo(CARD_LINE_MIN_SPACING_EM, 5);
		expect(overflowing.halfSpanEm).toBeGreaterThan(maxHalfSpan);
		expect(overflowing.maxScrollEm).toBeCloseTo(overflowing.halfSpanEm, 5);
	});

	it("never reports scroll range while the row still fits", () => {
		for (const count of [1, 3, 5, 7]) {
			expect(computeHandLine(count, 10).maxScrollEm).toBe(0);
		}
	});

	it("pans the row by the scroll offset and clamps it to the available range", () => {
		const maxHalfSpan = 10;
		const centered = computeHandLine(20, maxHalfSpan);
		const scrolled = computeHandLine(20, maxHalfSpan, 3);
		expect(scrolled.scrollEm).toBe(3);
		for (const [i, slot] of scrolled.slots.entries()) {
			expect(slot.x).toBeCloseTo(centered.slots[i].x - 3, 5);
		}

		const overscrolled = computeHandLine(20, maxHalfSpan, 9999);
		expect(overscrolled.scrollEm).toBeCloseTo(centered.maxScrollEm, 5);
		expect(computeHandLine(20, maxHalfSpan, -9999).scrollEm).toBeCloseTo(-centered.maxScrollEm, 5);
	});

	it("ignores scroll entirely when there is nothing to scroll", () => {
		expect(computeHandLine(4, 10, 50).scrollEm).toBe(0);
		expect(computeHandLine(4, 10, 50).slots[0].x).toBeCloseTo(-1.5 * CARD_LINE_SPACING_EM, 5);
	});

	it("never rotates any card", () => {
		for (const slot of computeHandLine(8).slots) {
			expect(slot.rotateDeg).toBe(0);
		}
	});
});

describe("centerSlotIndex", () => {
	it("returns center slot of viewport", () => {
		const layout = computeHandLine(7, 10, 0);
		expect(centerSlotIndex(layout)).toBe(3);
	});

	it("returns -1 for empty layout", () => {
		const layout = computeHandLine(0, 10, 0);
		expect(centerSlotIndex(layout)).toBe(-1);
	});

	it("returns slot closest to x=0 when scrolled", () => {
		const layout = computeHandLine(7, 6, 6);
		expect(centerSlotIndex(layout)).toBe(5);
	});
});
