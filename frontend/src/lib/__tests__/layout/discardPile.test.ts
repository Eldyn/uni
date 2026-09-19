import { describe, it, expect } from "vitest";

import {
	appendDiscard,
	discardEntryFor,
	discardStackZ,
	discardStepFor,
	DISCARD_CAP,
	DISCARD_STACK_STEP,
	MAX_DISCARD_HEIGHT,
	MIN_DISCARD_STEP,
	MAX_ROTATION_DEG,
	paintTopWild,
	type DiscardEntry
} from "$components/game/layout/discardPile";
import type { Card } from "$stores/game.svelte";

function card(id: number): Card {
	return { id, type: "red", value: "5" };
}

function wild(id: number): Card {
	return { id, type: "white", value: "jolly" };
}

describe("discardEntryFor", () => {
	it("gives a stable rotation and jitter for the same card id + seq", () => {
		const a = discardEntryFor(card(42), 7);
		const b = discardEntryFor(card(42), 7);
		expect(a.rotationDeg).toBe(b.rotationDeg);
		expect(a.jitter).toEqual(b.jitter);
	});

	it("scatters the same card differently when it cycles back through the pile", () => {
		const first = discardEntryFor(card(42), 1);
		const again = discardEntryFor(card(42), 2);
		expect(first.rotationDeg).not.toBe(again.rotationDeg);
	});

	it("keeps rotation within the scatter bound", () => {
		for (let id = 0; id < 50; id++) {
			expect(Math.abs(discardEntryFor(card(id)).rotationDeg)).toBeLessThanOrEqual(MAX_ROTATION_DEG);
		}
	});
});

describe("appendDiscard", () => {
	it("appends a new top card, keeping the input untouched", () => {
		const history: DiscardEntry[] = [];
		const next = appendDiscard(history, card(1));
		expect(history).toEqual([]);
		expect(next).toHaveLength(1);
		expect(next[0].card.id).toBe(1);
	});

	it("ignores a re-reveal of the current top card", () => {
		let history = appendDiscard([], card(1));
		history = appendDiscard(history, card(1));
		expect(history).toHaveLength(1);
	});

	it("evicts the oldest first once the cap is exceeded", () => {
		let history: DiscardEntry[] = [];
		for (let id = 0; id < DISCARD_CAP + 5; id++) {
			history = appendDiscard(history, card(id));
		}
		expect(history).toHaveLength(DISCARD_CAP);
		// oldest five (ids 0..4) fell off the front; newest is on the end
		expect(history[0].card.id).toBe(5);
		expect(history[history.length - 1].card.id).toBe(DISCARD_CAP + 4);
	});

	it("gives every entry a monotonically increasing seq, even for recycled ids", () => {
		let history = appendDiscard([], card(1));
		history = appendDiscard(history, card(2));
		// id 1 recycled back out of the reshuffled draw pile
		history = appendDiscard(history, card(1));
		expect(history.map((e) => e.seq)).toEqual([1, 2, 3]);
	});

	it("keeps seq growing across cap evictions (never reuses a live seq)", () => {
		let history: DiscardEntry[] = [];
		for (let id = 0; id < 10; id++) history = appendDiscard(history, card(id), 3);
		const seqs = history.map((e) => e.seq);
		expect(new Set(seqs).size).toBe(seqs.length);
		expect(seqs).toEqual([...seqs].sort((a, b) => a - b));
	});

	it("respects a custom cap", () => {
		let history: DiscardEntry[] = [];
		for (let id = 0; id < 10; id++) history = appendDiscard(history, card(id), 3);
		expect(history).toHaveLength(3);
		expect(history.map((e) => e.card.id)).toEqual([7, 8, 9]);
	});
});

describe("paintTopWild", () => {
	it("records the chosen color on a wild sitting on top", () => {
		const history = paintTopWild(appendDiscard([], wild(1)), "green");
		expect(history[0].wildColor).toBe("green");
	});

	it("leaves the color once the wild is buried, rather than following the table", () => {
		let history = paintTopWild(appendDiscard([], wild(1)), "green");
		history = appendDiscard(history, card(2));
		history = paintTopWild(history, "red");
		expect(history[0].wildColor).toBe("green");
		expect(history[1].wildColor).toBeUndefined();
	});

	it("treats white as no choice at all", () => {
		const history = paintTopWild(appendDiscard([], wild(1)), "white");
		expect(history[0].wildColor).toBeUndefined();
	});

	it("never paints a colored card", () => {
		const history = appendDiscard([], card(1));
		expect(paintTopWild(history, "green")).toBe(history);
	});

	it("returns the same array when there is nothing to change", () => {
		const history = paintTopWild(appendDiscard([], wild(1)), "green");
		expect(paintTopWild(history, "green")).toBe(history);
		expect(paintTopWild([], "green")).toEqual([]);
	});
});

describe("discardStepFor", () => {
	it("uses the comfortable step until the height budget is reached", () => {
		expect(discardStepFor(1)).toBeCloseTo(DISCARD_STACK_STEP);
		// MAX_DISCARD_HEIGHT / 24 == DISCARD_STACK_STEP, so 24 is the last
		// uncompressed pile.
		expect(discardStepFor(24)).toBeCloseTo(DISCARD_STACK_STEP);
	});

	it("compresses above the budget, floored at the precision limit", () => {
		expect(discardStepFor(60)).toBeCloseTo(MAX_DISCARD_HEIGHT / 60);
		// 0.6 / 150 == MIN_DISCARD_STEP; larger piles hold the floor.
		expect(discardStepFor(150)).toBeCloseTo(MIN_DISCARD_STEP);
		expect(discardStepFor(1000)).toBe(MIN_DISCARD_STEP);
	});

	it("never drops below the precision floor, for any pile size", () => {
		for (const total of [1, 24, 25, 60, 150, 1000, 100000]) {
			expect(discardStepFor(total)).toBeGreaterThanOrEqual(MIN_DISCARD_STEP);
		}
	});
});

describe("discardStackZ", () => {
	it("gives every card its own depth, at any pile size", () => {
		// Regression: cards used to be collapsed onto a shared bottom tier past a
		// 20-card cap. Multiple planes at one Y render as dense Z-fighting stripes
		// across the pile, so no two indices may ever share a depth.
		for (const total of [1, 10, 20, 30, 60, 150, DISCARD_CAP, 1000]) {
			const seen = new Set<number>();
			for (let i = 0; i < total; i++) {
				const y = discardStackZ(i, total);
				expect(seen.has(y)).toBe(false);
				seen.add(y);
			}
		}
	});

	it("is strictly increasing with index", () => {
		const total = 60;
		for (let i = 0; i < total - 1; i++) {
			expect(discardStackZ(i + 1, total)).toBeGreaterThan(discardStackZ(i, total));
		}
	});

	it("bounds the pile height by compressing, not by truncating", () => {
		// 30 cards would be 0.75 at the comfortable step; compression fits it in
		// the budget while still giving all 30 cards distinct depths.
		const total = 30;
		expect(discardStackZ(total - 1, total)).toBeLessThanOrEqual(MAX_DISCARD_HEIGHT + 1e-9);
		expect(discardStackZ(total - 1, total)).toBeGreaterThan(0);
	});

	it("does not grow past the budget until the step floors", () => {
		const total = 150;
		expect(discardStackZ(total - 1, total)).toBeLessThanOrEqual(MAX_DISCARD_HEIGHT + 1e-9);
		// Past the floor the pile must grow again rather than lose cards.
		expect(discardStackZ(999, 1000)).toBeGreaterThan(0);
	});
});
