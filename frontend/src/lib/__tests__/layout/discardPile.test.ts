import { describe, it, expect } from "vitest";

import {
	appendDiscard,
	discardEntryFor,
	DISCARD_CAP,
	type DiscardEntry
} from "$components/game/layout/discardPile";
import type { Card } from "$stores/game.svelte";

function card(id: number): Card {
	return { id, type: "red", value: "5" };
}

describe("discardEntryFor", () => {
	it("gives a stable rotation and jitter for the same card id", () => {
		const a = discardEntryFor(card(42));
		const b = discardEntryFor(card(42));
		expect(a.rotationDeg).toBe(b.rotationDeg);
		expect(a.jitter).toEqual(b.jitter);
	});

	it("keeps rotation within the scatter bound", () => {
		for (let id = 0; id < 50; id++) {
			expect(Math.abs(discardEntryFor(card(id)).rotationDeg)).toBeLessThanOrEqual(16);
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

	it("respects a custom cap", () => {
		let history: DiscardEntry[] = [];
		for (let id = 0; id < 10; id++) history = appendDiscard(history, card(id), 3);
		expect(history).toHaveLength(3);
		expect(history.map((e) => e.card.id)).toEqual([7, 8, 9]);
	});
});
