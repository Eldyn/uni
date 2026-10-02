import { describe, expect, it } from "vitest";
import { stableIndex } from "../../chat/playLog/stableIndex";

describe("stableIndex", () => {
	it("is deterministic for the same input", () => {
		expect(stableIndex(142, "skip:three", 4)).toBe(stableIndex(142, "skip:three", 4));
	});

	it("stays in range", () => {
		for (let seq = 0; seq < 200; seq++) {
			const i = stableIndex(seq, "draw:generic", 3);
			expect(i).toBeGreaterThanOrEqual(0);
			expect(i).toBeLessThan(3);
		}
	});

	it("returns 0 for empty or single pools", () => {
		expect(stableIndex(1, "x", 0)).toBe(0);
		expect(stableIndex(1, "x", 1)).toBe(0);
	});

	it("changes with seq", () => {
		const picks = new Set([1, 2, 3, 4, 5, 6, 7, 8].map((s) => stableIndex(s, "draw:generic", 4)));
		expect(picks.size).toBeGreaterThan(1);
	});

	it("matches reference FNV-1a values so every client keeps picking the same line", () => {
		expect(stableIndex(1, "draw", 7)).toBe(0);
		expect(stableIndex(42, "skip", 5)).toBe(2);
		expect(stableIndex(1000, "wild", 3)).toBe(0);
	});
});
