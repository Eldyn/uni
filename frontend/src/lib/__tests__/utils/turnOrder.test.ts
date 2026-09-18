import { describe, it, expect } from "vitest";
import { computeTurnOrderWindow } from "$utils/turnOrder";
import type { GamePlayer } from "$stores/game.svelte";

function player(username: string): GamePlayer {
	return { username, card_count: 7, is_bot: false };
}

describe("computeTurnOrderWindow", () => {
	it("always returns exactly `radius` prev/next slots, padding with null for small tables", () => {
		const players = [player("a"), player("b"), player("c")];
		const window = computeTurnOrderWindow(players, "a", 1, 2);
		expect(window.prev).toHaveLength(2);
		expect(window.next).toHaveLength(2);
		expect(window.current?.username).toBe("a");
	});

	it("repeats players in the window rather than collapsing it for tiny lobbies", () => {
		const players = [player("a"), player("b")];
		const window = computeTurnOrderWindow(players, "a", 1, 2);
		expect(window.next.map((p) => p?.username)).toEqual(["b", "a"]);
		expect(window.prev.map((p) => p?.username)).toEqual(["b", "a"]);
	});

	it("pads with null only when there genuinely aren't enough OTHER players (n <= 1)", () => {
		const players = [player("solo")];
		const window = computeTurnOrderWindow(players, "solo", 1, 2);
		expect(window.prev).toEqual([null, null]);
		expect(window.next).toEqual([null, null]);
	});

	it("respects reversed play direction, nearest-first-from-current on each side", () => {
		const players = [player("a"), player("b"), player("c"), player("d")];
		const window = computeTurnOrderWindow(players, "c", -1, 2);
		expect(window.next.map((p) => p?.username)).toEqual(["b", "a"]);
		expect(window.prev.map((p) => p?.username)).toEqual(["d", "a"]);
	});

	it("guards against a missing current turn or empty players with radius-length null slots", () => {
		expect(computeTurnOrderWindow([player("a"), player("b")], "nobody", 1)).toEqual({
			prev: [null, null],
			current: null,
			next: [null, null]
		});
		expect(computeTurnOrderWindow([], "a", 1)).toEqual({
			prev: [null, null],
			current: null,
			next: [null, null]
		});
	});
});
