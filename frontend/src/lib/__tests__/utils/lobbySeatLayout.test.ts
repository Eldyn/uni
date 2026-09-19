import { describe, it, expect } from "vitest";
import { computeSeatLayout, CARD_ASPECT } from "$utils/lobbySeatLayout";

describe("computeSeatLayout", () => {
	it("packs 16 seats into 4x4 on a phone-sized box", () => {
		const layout = computeSeatLayout({ boxWidth: 328, boxHeight: 424, count: 16 });
		expect(layout.cols).toBe(4);
		expect(layout.rows).toBe(4);
		expect(layout.cardWidth).toBeCloseTo(56, 0);
	});

	it("uses two rows of eight on a wide desktop box", () => {
		const layout = computeSeatLayout({ boxWidth: 1200, boxHeight: 600, count: 16, gap: 16 });
		expect(layout.cols).toBe(8);
		expect(layout.rows).toBe(2);
		expect(layout.cardWidth).toBeCloseTo(136, 0);
	});

	it("grows to hero cards and clamps to the max width for a small lobby", () => {
		const layout = computeSeatLayout({ boxWidth: 1200, boxHeight: 600, count: 4, gap: 16 });
		expect(layout.cols).toBe(4);
		expect(layout.rows).toBe(1);
		expect(layout.cardWidth).toBe(220);
	});

	it("places two players side by side in a landscape box", () => {
		const layout = computeSeatLayout({ boxWidth: 700, boxHeight: 400, count: 2 });
		expect(layout.cols).toBe(2);
		expect(layout.rows).toBe(1);
	});

	it("never returns fewer columns than one or a zero-sized card", () => {
		const layout = computeSeatLayout({ boxWidth: 0, boxHeight: 0, count: 8 });
		expect(layout.cols).toBeGreaterThanOrEqual(1);
		expect(layout.cardWidth).toBeGreaterThan(0);
	});

	it("keeps card height consistent with the card aspect ratio", () => {
		const layout = computeSeatLayout({ boxWidth: 900, boxHeight: 500, count: 9 });
		expect(layout.cardHeight).toBeCloseTo(layout.cardWidth / CARD_ASPECT, 5);
	});
});
