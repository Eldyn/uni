import { describe, it, expect } from "vitest";

import {
	buildMatchFixture,
	parseMatchFixtureQuery,
	FIXTURE_LOCAL_USERNAME
} from "$lib/dev/matchFixture";
import { TypeMap, ValueMap } from "$lib/generated/schemas";

describe("parseMatchFixtureQuery", () => {
	it("ignores a query that does not ask for the harness", () => {
		expect(parseMatchFixtureQuery("")).toBeNull();
		expect(parseMatchFixtureQuery("?players=8")).toBeNull();
		expect(parseMatchFixtureQuery("?dev=other")).toBeNull();
	});

	it("defaults to a four-player table", () => {
		const options = parseMatchFixtureQuery("?dev=match");
		expect(options).not.toBeNull();
		expect(options!.players).toBe(4);
		expect(options!.hand).toBe(7);
		expect(options!.direction).toBe(1);
	});

	it("reads every knob off the query string", () => {
		const options = parseMatchFixtureQuery("?dev=match&players=9&hand=14&turn=3&dir=-1&draws=6");
		expect(options).toEqual({
			players: 9,
			hand: 14,
			turn: 3,
			direction: -1,
			pendingDraws: 6,
			seed: 1,
			select: null,
			hover: null,
			wildTop: null
		});
	});

	it("clamps the player count to the server's seat cap", () => {
		expect(parseMatchFixtureQuery("?dev=match&players=99")!.players).toBe(16);
		expect(parseMatchFixtureQuery("?dev=match&players=0")!.players).toBe(2);
	});

	it("clamps the turn seat to the seats that exist", () => {
		expect(parseMatchFixtureQuery("?dev=match&players=3&turn=7")!.turn).toBe(2);
	});

	it("falls back to defaults on unparseable values", () => {
		const options = parseMatchFixtureQuery("?dev=match&players=abc&hand=");
		expect(options!.players).toBe(4);
		expect(options!.hand).toBe(7);
	});
});

describe("buildMatchFixture", () => {
	const options = parseMatchFixtureQuery("?dev=match&players=6&hand=11&turn=2&dir=-1&draws=4")!;

	it("seats the local player first, with a real hand, and bots after", () => {
		const state = buildMatchFixture(options);
		expect(state.players).toHaveLength(6);
		expect(state.players[0].username).toBe(FIXTURE_LOCAL_USERNAME);
		expect(state.players[0].is_bot).toBe(false);
		expect(state.players[0].hand).toHaveLength(11);
		expect(state.players[0].card_count).toBe(11);
		expect(state.players.slice(1).every((player) => player.is_bot)).toBe(true);
		expect(state.players.slice(1).every((player) => player.hand === undefined)).toBe(true);
	});

	it("gives every seat a distinct username", () => {
		const state = buildMatchFixture(parseMatchFixtureQuery("?dev=match&players=16")!);
		const names = new Set(state.players.map((player) => player.username));
		expect(names.size).toBe(16);
	});

	it("carries the requested turn, direction and pending draws", () => {
		const state = buildMatchFixture(options);
		expect(state.current_turn).toBe(state.players[2].username);
		expect(state.play_direction).toBe(-1);
		expect(state.pending_draws).toBe(4);
	});

	it("keeps the top card coloured so the active type is always defined", () => {
		for (let seed = 0; seed < 40; seed++) {
			const state = buildMatchFixture({ ...options, seed });
			expect(state.top_card!.type).not.toBe("white");
			expect(state.active_type).toBe(state.top_card!.type);
		}
	});

	it("only ever emits legal type/value pairs", () => {
		const state = buildMatchFixture({ ...options, hand: 40 });
		for (const card of state.players[0].hand!) {
			expect(TypeMap).toContain(card.type);
			expect(ValueMap).toContain(card.value);
			const isWild = card.value === "jolly" || card.value === "jolly_draw4";
			expect(card.type === "white").toBe(isWild);
		}
	});

	it("gives every card a unique id", () => {
		const state = buildMatchFixture({ ...options, hand: 40 });
		const ids = state.players[0].hand!.map((card) => card.id);
		expect(new Set([...ids, state.top_card!.id]).size).toBe(ids.length + 1);
	});

	it("flags exactly the cards that answer the top card", () => {
		const state = buildMatchFixture({ ...options, hand: 40 });
		const top = state.top_card!;
		for (const card of state.players[0].hand!) {
			const playable = card.type === "white" || card.type === top.type || card.value === top.value;
			expect(card.can_play).toBe(playable);
		}
	});

	it("deals opponents a non-empty hand count", () => {
		const state = buildMatchFixture(parseMatchFixtureQuery("?dev=match&players=16&seed=7")!);
		for (const player of state.players.slice(1)) {
			expect(player.card_count).toBeGreaterThan(0);
		}
	});

	it("renders identically for the same seed and differently across seeds", () => {
		expect(buildMatchFixture({ ...options, seed: 3 })).toEqual(
			buildMatchFixture({ ...options, seed: 3 })
		);
		expect(buildMatchFixture({ ...options, seed: 3 })).not.toEqual(
			buildMatchFixture({ ...options, seed: 4 })
		);
	});
});
