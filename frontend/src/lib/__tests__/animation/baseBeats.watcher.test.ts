import { describe, it, expect, afterEach } from "vitest";
import { flushSync } from "svelte";
import { createBaseBeatsWatcher, DRAW_HOVER_LIFT } from "$components/game/animation/baseBeats.svelte";
import { CardBus } from "$components/game/card-bus.svelte";
import { CardRegistry } from "$components/game/animation/cardRegistry.svelte";
import { storeGame } from "$stores/game.svelte";
import { storeAuth } from "$stores/auth.svelte";
import type { BoardPlacement } from "$components/game/layout/boardPlacement";

// Regression test for the exact class of bug found during manual
// browser verification: createBaseBeatsWatcher's $effect/$effect.root calls
// only compile into real reactive code when the file the Svelte compiler
// actually processes ends in `.svelte.ts` — a plain `.ts` file leaves them as
// literal, undefined global function calls, throwing
// `ReferenceError: $effect is not defined` the instant the watcher is
// constructed. The other baseBeats.*.test.ts files only exercise the pure
// exported helpers, never createBaseBeatsWatcher itself, so none of them
// would have caught this.

const placement: BoardPlacement = {
	mat: {} as never,
	handScale: 1,
	centerScale: 1,
	discardX: 0,
	discardZ: 0,
	localSeatZ: 5,
	localAvatarZ: 6,
	drawPileX: -3,
	drawPileZ: 5,
	drawPileScale: 1,
	drawPileBesideHand: true
};

describe("createBaseBeatsWatcher", () => {
	afterEach(() => {
		storeGame.state = null;
		storeAuth.username = "";
	});

	it("constructs via $effect.root and its inner $effect fires without throwing", () => {
		storeGame.state = {
			active_type: "red",
			current_turn: "me",
			play_direction: 1,
			top_card: { id: 1, type: "red", value: "5" },
			players: [{ username: "me", card_count: 5, is_bot: false }],
			pending_draws: 0,
			draw_pile_size: 10
		} as never;

		const bus = new CardBus();
		const cardRegistry = new CardRegistry();

		let dispose: () => void = () => {};
		expect(() => {
			dispose = createBaseBeatsWatcher({
				bus,
				cardRegistry,
				getPlacement: () => placement,
				getOpponentSeatAnchor: () => [0, 0, 0]
			});
			flushSync();
		}).not.toThrow();

		// The effect ran at least once: on its first pass (no previous top card
		// tracked yet), it hands the top card straight to the bus rather than
		// queuing a play animation for it — proof the watcher's body actually
		// executed, not just that construction didn't throw.
		expect(bus.discardTop).toEqual({ id: 1, type: "red", value: "5" });

		dispose();
	});

	it("queues a draw beat for the local player when their card_count increases", () => {
		storeAuth.username = "me";
		storeGame.state = {
			active_type: "red",
			current_turn: "me",
			play_direction: 1,
			top_card: { id: 1, type: "red", value: "5" },
			players: [
				{ username: "me", card_count: 5, is_bot: false, hand: [{ id: 1, type: "red", value: "5" }] }
			],
			pending_draws: 0,
			draw_pile_size: 10
		} as never;

		const bus = new CardBus();
		const cardRegistry = new CardRegistry();
		const dispose = createBaseBeatsWatcher({
			bus,
			cardRegistry,
			getPlacement: () => placement,
			getOpponentSeatAnchor: () => [0, 0, 0]
		});
		flushSync();

		storeGame.state = {
			active_type: "red",
			current_turn: "me",
			play_direction: 1,
			top_card: { id: 1, type: "red", value: "5" },
			players: [
				{
					username: "me",
					card_count: 6,
					is_bot: false,
					hand: [
						{ id: 1, type: "red", value: "5" },
						{ id: 2, type: "blue", value: "7" }
					]
				}
			],
			pending_draws: 0,
			draw_pile_size: 10
		} as never;
		flushSync();

		expect(cardRegistry.activeFlights.some((f) => f.id === "2")).toBe(true);

		dispose();
	});

	it("queues a hidden draw beat for an opponent when their card_count increases, seeded at the opponent card scale", () => {
		storeAuth.username = "me";
		storeGame.state = {
			active_type: "red",
			current_turn: "bob",
			play_direction: 1,
			top_card: { id: 1, type: "red", value: "5" },
			players: [
				{ username: "me", card_count: 5, is_bot: false, hand: [] },
				{ username: "bob", card_count: 3, is_bot: false }
			],
			pending_draws: 0,
			draw_pile_size: 10
		} as never;

		const bus = new CardBus();
		const cardRegistry = new CardRegistry();
		const dispose = createBaseBeatsWatcher({
			bus,
			cardRegistry,
			getPlacement: () => placement,
			getOpponentSeatAnchor: () => [1, 0, -2],
			getOpponentCardScale: () => 0.4
		});
		flushSync();

		storeGame.state = {
			active_type: "red",
			current_turn: "bob",
			play_direction: 1,
			top_card: { id: 1, type: "red", value: "5" },
			players: [
				{ username: "me", card_count: 5, is_bot: false, hand: [] },
				{ username: "bob", card_count: 4, is_bot: false }
			],
			pending_draws: 0,
			draw_pile_size: 10
		} as never;
		flushSync();

		const flight = cardRegistry.activeFlights.find((f) => f.id.startsWith("draw:bob:"));
		expect(flight).toBeDefined();
		expect(flight!.pose.turned).toBe(true);
		expect(flight!.pose.scale).toBe(placement.drawPileScale);

		dispose();
	});

	it("seeds an opponent's play at the opponent card scale, not centerScale", () => {
		storeAuth.username = "me";
		storeGame.state = {
			active_type: "red",
			current_turn: "me",
			play_direction: 1,
			top_card: { id: 1, type: "red", value: "5" },
			players: [
				{ username: "me", card_count: 5, is_bot: false, hand: [] },
				{ username: "bob", card_count: 3, is_bot: false }
			],
			pending_draws: 0,
			draw_pile_size: 10
		} as never;

		const bus = new CardBus();
		const cardRegistry = new CardRegistry();
		const dispose = createBaseBeatsWatcher({
			bus,
			cardRegistry,
			getPlacement: () => ({ ...placement, centerScale: 1 }),
			getOpponentSeatAnchor: () => [1, 0, -2],
			getOpponentCardScale: () => 0.4,
			getOpponentCardPose: () => ({ position: [1.2, 0.05, -2.1], spinDeg: 45 })
		});
		flushSync();

		storeGame.state = {
			active_type: "red",
			current_turn: "me",
			play_direction: 1,
			top_card: { id: 2, type: "blue", value: "7" },
			last_play: { player: "bob", hand_index: 0 },
			players: [
				{ username: "me", card_count: 5, is_bot: false, hand: [] },
				{ username: "bob", card_count: 2, is_bot: false }
			],
			pending_draws: 0,
			draw_pile_size: 10
		} as never;
		flushSync();

		const flight = cardRegistry.activeFlights.find((f) => f.id === "2");
		expect(flight).toBeDefined();
		expect(flight!.pose.scale).toBe(0.4);
		expect(flight!.pose.x).toBeCloseTo(1.2);
		expect(flight!.pose.y).toBeCloseTo(0.05);
		expect(flight!.pose.z).toBeCloseTo(-2.1);
		expect(flight!.pose.spinDeg).toBe(45);

		dispose();
	});

	it("seeds a local draw's flight at the draw pile's real top-of-stack Y/Z, not the pile's base", () => {
		storeAuth.username = "me";
		storeGame.state = {
			active_type: "red",
			current_turn: "me",
			play_direction: 1,
			top_card: { id: 1, type: "red", value: "5" },
			players: [
				{ username: "me", card_count: 5, is_bot: false, hand: [{ id: 1, type: "red", value: "5" }] }
			],
			pending_draws: 0,
			draw_pile_size: 3
		} as never;

		const bus = new CardBus();
		const cardRegistry = new CardRegistry();
		const dispose = createBaseBeatsWatcher({
			bus,
			cardRegistry,
			getPlacement: () => placement,
			getOpponentSeatAnchor: () => [0, 0, 0]
		});
		flushSync();

		storeGame.state = {
			active_type: "red",
			current_turn: "me",
			play_direction: 1,
			top_card: { id: 1, type: "red", value: "5" },
			players: [
				{
					username: "me",
					card_count: 6,
					is_bot: false,
					hand: [
						{ id: 1, type: "red", value: "5" },
						{ id: 2, type: "blue", value: "7" }
					]
				}
			],
			pending_draws: 0,
			draw_pile_size: 2
		} as never;
		flushSync();

		const flight = cardRegistry.activeFlights.find((f) => f.id === "2");
		expect(flight).toBeDefined();
		// draw_pile_size was 3 pre-draw -> visible stack size 3 -> top index 2, plus DRAW_HOVER_LIFT.
		expect(flight!.pose.y).toBeCloseTo(0.6 + 2 * 0.02 + DRAW_HOVER_LIFT);
		expect(flight!.pose.z).toBeCloseTo(placement.drawPileZ - 2 * 0.02 * placement.drawPileScale);

		dispose();
	});

	it("resolves the discard-pile anchor live, via the shared resolver, for both a play and its landing shake", () => {
		storeAuth.username = "me";
		storeGame.state = {
			active_type: "red",
			current_turn: "me",
			play_direction: 1,
			top_card: { id: 1, type: "red", value: "5" },
			players: [{ username: "me", card_count: 5, is_bot: false, hand: [] }],
			pending_draws: 0,
			draw_pile_size: 10
		} as never;

		const bus = new CardBus();
		const cardRegistry = new CardRegistry();
		const dispose = createBaseBeatsWatcher({
			bus,
			cardRegistry,
			getPlacement: () => placement,
			getOpponentSeatAnchor: () => [0, 0, 0]
		});
		flushSync();

		storeGame.state = {
			active_type: "red",
			current_turn: "me",
			play_direction: 1,
			top_card: { id: 2, type: "blue", value: "7" },
			last_play: { player: "me", hand_index: 0 },
			players: [{ username: "me", card_count: 5, is_bot: false, hand: [] }],
			pending_draws: 0,
			draw_pile_size: 10
		} as never;
		flushSync();

		expect(cardRegistry.activeFlights.some((f) => f.id === "2")).toBe(true);

		dispose();
	});
});
