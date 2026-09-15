import { describe, it, expect } from "vitest";

import {
	appendDiscard,
	discardEntryFor,
	discardStackZ,
	BURIED_DEPTH_CAP,
	DISCARD_CAP,
	DISCARD_STACK_STEP,
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
			expect(Math.abs(discardEntryFor(card(id)).rotationDeg)).toBeLessThanOrEqual(
				MAX_ROTATION_DEG
			);
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

describe("discardStackZ", () => {
	it("caps Z stack depth for buried cards beyond 20 items", () => {
		expect(BURIED_DEPTH_CAP).toBe(20);

		// Single parameter index clamp
		expect(discardStackZ(20)).toBeCloseTo(20 * DISCARD_STACK_STEP);
		expect(discardStackZ(25)).toBe(discardStackZ(20));
		expect(discardStackZ(100)).toBe(discardStackZ(20));

		// Full pile with 30 cards: buried cards (depth >= 20 from top) share tier 0
		const total = 30;
		expect(discardStackZ(0, total)).toBe(0);
		expect(discardStackZ(9, total)).toBe(0);
		// Card right above buried tier gets tier 1
		expect(discardStackZ(10, total)).toBeCloseTo(1 * DISCARD_STACK_STEP);
		// Top card gets capped at 20 tiers
		expect(discardStackZ(29, total)).toBeCloseTo(20 * DISCARD_STACK_STEP);

		// Unbounded pile of 100 cards
		expect(discardStackZ(0, 100)).toBe(0);
		expect(discardStackZ(79, 100)).toBe(0);
		expect(discardStackZ(80, 100)).toBeCloseTo(1 * DISCARD_STACK_STEP);
		expect(discardStackZ(99, 100)).toBeCloseTo(20 * DISCARD_STACK_STEP);
	});

	it("stacks all cards linearly when total count is within depth 20", () => {
		const total = 10;
		for (let i = 0; i < total; i++) {
			expect(discardStackZ(i, total)).toBeCloseTo(i * DISCARD_STACK_STEP);
		}
	});
});

