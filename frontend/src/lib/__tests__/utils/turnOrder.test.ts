import { describe, it, expect } from "vitest";

import { computeTurnOrderWindow } from "$utils/turnOrder";
import type { GamePlayer } from "$stores/game.svelte";

function makePlayers(usernames: string[]): GamePlayer[] {
	return usernames.map((username) => ({ username, card_count: 7, is_bot: false }));
}

describe("computeTurnOrderWindow", () => {
	it("returns an empty window when the current turn isn't found", () => {
		const players = makePlayers(["a", "b"]);
		expect(computeTurnOrderWindow(players, "ghost", 1)).toEqual({
			prev: [],
			current: null,
			next: []
		});
	});

	it("returns an empty window for an empty player list", () => {
		expect(computeTurnOrderWindow([], "a", 1)).toEqual({ prev: [], current: null, next: [] });
	});

	it("shows both neighbors for a 5-player table without duplicates", () => {
		const players = makePlayers(["a", "b", "c", "d", "e"]);
		const w = computeTurnOrderWindow(players, "c", 1);
		expect(w.current?.username).toBe("c");
		expect(w.prev.map((p) => p.username)).toEqual(["b", "a"]);
		expect(w.next.map((p) => p.username)).toEqual(["d", "e"]);
	});

	it("respects reversed play direction", () => {
		const players = makePlayers(["a", "b", "c", "d", "e"]);
		const w = computeTurnOrderWindow(players, "c", -1);
		expect(w.prev.map((p) => p.username)).toEqual(["d", "e"]);
		expect(w.next.map((p) => p.username)).toEqual(["b", "a"]);
	});

	it("wraps around the table edges", () => {
		const players = makePlayers(["a", "b", "c", "d", "e"]);
		const w = computeTurnOrderWindow(players, "a", 1);
		expect(w.prev.map((p) => p.username)).toEqual(["e", "d"]);
		expect(w.next.map((p) => p.username)).toEqual(["b", "c"]);
	});

	it("caps the radius so a small table never repeats a player on both sides", () => {
		const players = makePlayers(["a", "b", "c", "d"]);
		const w = computeTurnOrderWindow(players, "a", 1);
		const shown = new Set([
			...w.prev.map((p) => p.username),
			w.current?.username,
			...w.next.map((p) => p.username)
		]);
		expect(shown.size).toBe(w.prev.length + w.next.length + 1);
	});

	it("shows nothing on either side for a 2-player table", () => {
		const players = makePlayers(["a", "b"]);
		const w = computeTurnOrderWindow(players, "a", 1);
		expect(w.prev).toEqual([]);
		expect(w.next).toEqual([]);
		expect(w.current?.username).toBe("a");
	});
});
