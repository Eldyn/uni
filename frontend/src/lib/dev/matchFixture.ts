/**
 * @file matchFixture.ts
 * @brief Deterministic synthetic match states for the local screenshot harness.
 * Pure: builds a `GameState` from a query string, touches no store and opens no
 * socket, so the board can be rendered offline for any player count.
 */

import { TypeMap, ValueMap } from "$lib/generated/schemas";
import type { Card, GamePlayer, GameState } from "$stores/game.svelte";

/** Username the harness assigns to the local player. */
export const FIXTURE_LOCAL_USERNAME = "You";

/** Mirrors `contract::kMaxLobbyMembers`, the server-side seat cap. */
const MAX_PLAYERS = 16;
const MIN_PLAYERS = 2;

/** Card ids start high so they can never collide with a real match's ids. */
const FIRST_CARD_ID = 60000;

/** Indices 0-3 of `TypeMap` are the four playable colours; 4 is "white". */
const COLOUR_COUNT = 4;

/** Indices 0-12 of `ValueMap` are the colour-legal values (0-9, skip, reverse, +2). */
const COLOUR_VALUE_COUNT = 13;

/** How often a generated card is a wild instead of a coloured one. */
const WILD_CHANCE = 0.12;

/**
 * Opponent names of deliberately mixed length, cycled by seat index so seat
 * labels get stress-tested (truncation, wrapping) at every player count.
 */
const OPPONENT_NAMES = [
	"Bea",
	"Marco",
	"xX_dragonslayer_Xx",
	"Kim",
	"Alessandro",
	"nova",
	"Jul",
	"thequickbrownfox",
	"Ren",
	"Priya",
	"MOONWALKER",
	"Otto",
	"sam",
	"Valentina",
	"zz"
];

/**
 * @interface MatchFixtureOptions
 * @brief Fully resolved knobs of a synthetic match, after query parsing.
 */
/** Cards left in the fixture's draw pile, so both centre piles render. */
const FIXTURE_DRAW_PILE_SIZE = 40;

export interface MatchFixtureOptions {
	/** Total seats at the table, local player included. */
	players: number;
	/** Cards in the local player's hand. */
	hand: number;
	/** Seat index (0 = local player) whose turn it is. */
	turn: number;
	/** Play direction, 1 clockwise or -1 counter-clockwise. */
	direction: number;
	/** Accumulated +2/+4 chain shown by the HUD. */
	pendingDraws: number;
	/** PRNG seed, so the same URL always renders the same table. */
	seed: number;
	/** Hand index to pre-arm as picked (touch selection), or null. Screenshotting
	 *  the halo/pulse states needs them reachable without a live gesture. */
	select: number | null;
	/** Hand index to pre-lift as hovered (mouse preview), or null. */
	hover: number | null;
	/** Forces the discard's top card to be a wild, so the "wild painted with the
	 *  color that was chosen for it" state is reachable without playing a real
	 *  match. The active colour stays whatever the fixture picked. */
	wildTop: "jolly" | "jolly_draw4" | null;
}

const DEFAULTS: MatchFixtureOptions = {
	players: 4,
	hand: 7,
	turn: 0,
	direction: 1,
	pendingDraws: 0,
	seed: 1,
	select: null,
	hover: null,
	wildTop: null
};

/**
 * @brief Small, fast, seedable PRNG (mulberry32).
 * Keeps fixtures reproducible across runs so screenshots are comparable.
 */
function mulberry32(seed: number): () => number {
	let state = seed >>> 0;
	return () => {
		state = (state + 0x6d2b79f5) >>> 0;
		let mixed = Math.imul(state ^ (state >>> 15), 1 | state);
		mixed = (mixed + Math.imul(mixed ^ (mixed >>> 7), 61 | mixed)) ^ mixed;
		return ((mixed ^ (mixed >>> 14)) >>> 0) / 4294967296;
	};
}

function clampInt(raw: string | null, min: number, max: number, fallback: number): number {
	if (raw === null) return fallback;
	const parsed = Number.parseInt(raw, 10);
	if (Number.isNaN(parsed)) return fallback;
	return Math.max(min, Math.min(max, parsed));
}

/**
 * @brief Reads the fixture knobs out of a `location.search` string.
 * @param search Query string, with or without its leading "?".
 * @returns Resolved options, or null when `dev=match` is absent.
 */
export function parseMatchFixtureQuery(search: string): MatchFixtureOptions | null {
	const params = new URLSearchParams(search);
	if (params.get("dev") !== "match") return null;

	const players = clampInt(params.get("players"), MIN_PLAYERS, MAX_PLAYERS, DEFAULTS.players);
	const hand = clampInt(params.get("hand"), 1, 40, DEFAULTS.hand);

	return {
		players,
		hand,
		turn: clampInt(params.get("turn"), 0, players - 1, DEFAULTS.turn),
		direction: params.get("dir") === "-1" ? -1 : DEFAULTS.direction,
		pendingDraws: clampInt(params.get("draws"), 0, 20, DEFAULTS.pendingDraws),
		seed: clampInt(params.get("seed"), 0, Number.MAX_SAFE_INTEGER, DEFAULTS.seed),
		select: params.has("select") ? clampInt(params.get("select"), 0, hand - 1, 0) : null,
		hover: params.has("hover") ? clampInt(params.get("hover"), 0, hand - 1, 0) : null,
		wildTop:
			params.get("wildtop") === "jolly" || params.get("wildtop") === "jolly_draw4"
				? (params.get("wildtop") as "jolly" | "jolly_draw4")
				: DEFAULTS.wildTop
	};
}

/**
 * @brief Builds one card, wild or coloured, drawing from the given PRNG.
 */
function makeCard(random: () => number, id: number): Card {
	if (random() < WILD_CHANCE) {
		return {
			id,
			type: "white",
			value: random() < 0.5 ? "jolly" : "jolly_draw4"
		};
	}

	return {
		id,
		type: TypeMap[Math.floor(random() * COLOUR_COUNT)],
		value: ValueMap[Math.floor(random() * COLOUR_VALUE_COUNT)]
	};
}

/**
 * @brief Assembles a complete synthetic `GameState` for the given options.
 * The local player sits at seat 0 and always holds a real hand; every other
 * seat is a bot carrying only a card count, exactly as the server sends it.
 */
export function buildMatchFixture(options: MatchFixtureOptions): GameState {
	const random = mulberry32(options.seed);
	let nextId = FIRST_CARD_ID;

	const hand = Array.from({ length: options.hand }, () => makeCard(random, nextId++));

	// A plausible spread of opponent hands: never empty, never absurd, and
	// derived from the seed so the ring's card fans stay identical per URL.
	const players: GamePlayer[] = [
		{ username: FIXTURE_LOCAL_USERNAME, card_count: hand.length, hand, is_bot: false }
	];
	for (let seat = 1; seat < options.players; seat++) {
		players.push({
			username: OPPONENT_NAMES[(seat - 1) % OPPONENT_NAMES.length],
			card_count: 1 + Math.floor(random() * 12),
			is_bot: true
		});
	}

	// The top card fixes the active colour, so a wild top card would leave the
	// board without one — keep the discard's face coloured. `wildTop` swaps the
	// face for a wild while keeping that colour, which is precisely the state a
	// wild reaches once its owner has been asked what it should become.
	const activeType = TypeMap[Math.floor(random() * COLOUR_COUNT)];
	const topCard: Card = {
		id: nextId++,
		type: options.wildTop ? "white" : activeType,
		value: options.wildTop ?? ValueMap[Math.floor(random() * COLOUR_VALUE_COUNT)]
	};

	// Mark the cards that legally answer the top card, mirroring the server's
	// `can_play` hint the hand uses to dim unplayable cards.
	for (const card of hand) {
		card.can_play =
			card.type === "white" || card.type === activeType || card.value === topCard.value;
	}

	return {
		active_type: activeType,
		current_turn: players[options.turn].username,
		play_direction: options.direction,
		top_card: topCard,
		players,
		pending_draws: options.pendingDraws,
		draw_pile_size: FIXTURE_DRAW_PILE_SIZE
	};
}
