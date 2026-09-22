import { describe, it, expect, afterEach } from "vitest";
import { flushSync } from "svelte";
import {
	createBaseBeatsWatcher,
	DRAW_HOVER_LIFT
} from "$components/game/animation/baseBeats.svelte";
import { CardBus } from "$components/game/card-bus.svelte";
import { CardRegistry } from "$components/game/animation/cardRegistry.svelte";
import { storeGame, Action } from "$stores/game.svelte";
import { storeAuth } from "$stores/auth.svelte";
import type { BoardPlacement } from "$components/game/layout/boardPlacement";
import { drawPileTopPose } from "$components/game/layout/drawPile";

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
		storeGame.actionRequired = null;
		storeGame.actionContext = null;
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

	it("seeds a local draw's flight from the draw pile's hovered (slid) pose", () => {
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
		// DrawPile3D is mid-hover: the top card has slid this far toward the player.
		bus.setDrawPileHoverDipZ(0.06);
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
		// Same resting top as the test above, slid forward by the hover offset.
		expect(flight!.pose.z).toBeCloseTo(
			placement.drawPileZ - 2 * 0.02 * placement.drawPileScale + 0.06
		);

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

	it("stops local playable draw at the flip on draw pile and records pendingLocalPlayDrawnId", () => {
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

		storeGame.actionRequired = Action.PlayDrawn;
		storeGame.actionContext = { card_id: 2 };
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
						{ id: 2, type: "red", value: "7" }
					]
				}
			],
			pending_draws: 0,
			draw_pile_size: 9
		} as never;
		flushSync();

		expect(bus.pendingLocalPlayDrawnId).toBe(2);
		const flight = cardRegistry.activeFlights.find((f) => f.id === "2");
		expect(flight).toBeDefined();
		const topPose = drawPileTopPose(placement, 10);
		expect(flight!.pose.x).toBeCloseTo(topPose[0]);
		expect(flight!.pose.y).toBeCloseTo(topPose[1] + DRAW_HOVER_LIFT);
		expect(flight!.pose.z).toBeCloseTo(topPose[2]);

		cardRegistry.flushImmediately();
		const flightAfterFlip = cardRegistry.activeFlights.find((f) => f.id === "2");
		expect(flightAfterFlip).toBeDefined();
		expect(flightAfterFlip!.pose.x).toBeCloseTo(topPose[0]);
		expect(flightAfterFlip!.pose.y).toBeCloseTo(topPose[1] + DRAW_HOVER_LIFT);
		expect(flightAfterFlip!.pose.z).toBeCloseTo(topPose[2]);
		expect(flightAfterFlip!.pose.turned).toBe(false);

		// Case 2A: Play it
		storeGame.actionRequired = null;
		storeGame.state = {
			active_type: "red",
			current_turn: "bob",
			play_direction: 1,
			top_card: { id: 2, type: "red", value: "7" },
			last_play: { player: "me", hand_index: 0 },
			players: [
				{ username: "me", card_count: 5, is_bot: false, hand: [{ id: 1, type: "red", value: "5" }] }
			],
			pending_draws: 0,
			draw_pile_size: 9
		} as never;
		flushSync();

		expect(bus.pendingLocalPlayDrawnId).toBeNull();
		expect(cardRegistry.activeFlights.some((f) => f.id === "2")).toBe(true);

		dispose();
	});

	it("flows held local card into hand when kept", async () => {
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

		storeGame.actionRequired = Action.PlayDrawn;
		storeGame.actionContext = { card_id: 2 };
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
						{ id: 2, type: "red", value: "7" }
					]
				}
			],
			pending_draws: 0,
			draw_pile_size: 9
		} as never;
		flushSync();

		expect(bus.pendingLocalPlayDrawnId).toBe(2);
		cardRegistry.flushImmediately();
		const flightAfterFlip2 = cardRegistry.activeFlights.find((f) => f.id === "2");
		expect(flightAfterFlip2).toBeDefined();
		expect(flightAfterFlip2!.pose.turned).toBe(false);

		// Case 2B: Keep it (actionRequired clears, top_card doesn't change)
		storeGame.actionRequired = null;
		storeGame.state = {
			active_type: "red",
			current_turn: "bob",
			play_direction: 1,
			top_card: { id: 1, type: "red", value: "5" },
			players: [
				{
					username: "me",
					card_count: 6,
					is_bot: false,
					hand: [
						{ id: 1, type: "red", value: "5" },
						{ id: 2, type: "red", value: "7" }
					]
				}
			],
			pending_draws: 0,
			draw_pile_size: 9
		} as never;
		flushSync();
		cardRegistry.flushImmediately();

		cardRegistry.flushImmediately();
		await Promise.resolve();

		expect(bus.pendingLocalPlayDrawnId).toBeNull();

		dispose();
	});

	it("handles opponent playable draw: moves to front holding spot, then plays to discard", () => {
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
		const frontPose = { position: [1.5, 0.05, -2.5] as [number, number, number], spinDeg: 120 };
		const dispose = createBaseBeatsWatcher({
			bus,
			cardRegistry,
			getPlacement: () => placement,
			getOpponentSeatAnchor: () => [1, 0, -2],
			getOpponentCardScale: () => 0.4,
			getOpponentFrontPose: () => frontPose
		});
		flushSync();

		// Bob draws 1 card, and current_turn remains "bob" (playable draw!)
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
			draw_pile_size: 9
		} as never;
		flushSync();

		expect(bus.isHoldingOpponent("bob")).toBe(true);
		const drawFlight = cardRegistry.activeFlights.find((f) => f.id.startsWith("draw:bob:"));
		expect(drawFlight).toBeDefined();

		cardRegistry.flushImmediately();

		// Bob plays it
		storeGame.state = {
			active_type: "red",
			current_turn: "me",
			play_direction: 1,
			top_card: { id: 2, type: "red", value: "7" },
			last_play: { player: "bob", hand_index: 0 },
			players: [
				{ username: "me", card_count: 5, is_bot: false, hand: [] },
				{ username: "bob", card_count: 3, is_bot: false }
			],
			pending_draws: 0,
			draw_pile_size: 9
		} as never;
		flushSync();

		expect(bus.isHoldingOpponent("bob")).toBe(false);
		const playFlight = cardRegistry.activeFlights.find((f) => f.id === "2");
		expect(playFlight).toBeDefined();
		expect(playFlight!.pose.x).toBeCloseTo(frontPose.position[0]);
		expect(playFlight!.pose.y).toBeCloseTo(frontPose.position[1]);
		expect(playFlight!.pose.z).toBeCloseTo(frontPose.position[2]);
		expect(playFlight!.pose.spinDeg).toBe(frontPose.spinDeg);

		dispose();
	});

	it("handles opponent playable draw: moves to front holding spot, then keeps card (turn passes)", async () => {
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
		const frontPose = { position: [1.5, 0.05, -2.5] as [number, number, number], spinDeg: 120 };
		const dispose = createBaseBeatsWatcher({
			bus,
			cardRegistry,
			getPlacement: () => placement,
			getOpponentSeatAnchor: () => [1, 0, -2],
			getOpponentCardScale: () => 0.4,
			getOpponentFrontPose: () => frontPose
		});
		flushSync();

		// Bob draws 1 card, current_turn is bob
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
			draw_pile_size: 9
		} as never;
		flushSync();

		expect(bus.isHoldingOpponent("bob")).toBe(true);
		cardRegistry.flushImmediately();

		// Bob passes/keeps: current_turn becomes "me", no last_play
		storeGame.state = {
			active_type: "red",
			current_turn: "me",
			play_direction: 1,
			top_card: { id: 1, type: "red", value: "5" },
			players: [
				{ username: "me", card_count: 5, is_bot: false, hand: [] },
				{ username: "bob", card_count: 4, is_bot: false }
			],
			pending_draws: 0,
			draw_pile_size: 9
		} as never;
		flushSync();

		cardRegistry.flushImmediately();
		await Promise.resolve();

		// Flowed back to Case 1 (into hand)
		expect(bus.isHoldingOpponent("bob")).toBe(false);

		dispose();
	});

	it("tracks in-flight draw count and passes dimmed to decoration during opponent multi-draw", async () => {
		storeAuth.username = "me";
		storeGame.state = {
			active_type: "red",
			current_turn: "me",
			play_direction: 1,
			top_card: { id: 1, type: "red", value: "draw2" },
			players: [
				{ username: "me", card_count: 5, is_bot: false, hand: [] },
				{ username: "bob", card_count: 3, is_bot: false }
			],
			pending_draws: 2,
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

		// Bob takes +2
		storeGame.state = {
			active_type: "red",
			current_turn: "me",
			play_direction: 1,
			top_card: { id: 1, type: "red", value: "draw2" },
			players: [
				{ username: "me", card_count: 5, is_bot: false, hand: [] },
				{ username: "bob", card_count: 5, is_bot: false }
			],
			pending_draws: 0,
			draw_pile_size: 8
		} as never;
		flushSync();

		expect(bus.getInFlightDrawCount("bob")).toBe(2);
		const bobFlights = cardRegistry.activeFlights.filter((f) => f.id.startsWith("draw:bob:"));
		expect(bobFlights).toHaveLength(2);
		// Bob is not current turn or target -> dimmed
		expect(bobFlights[0].decoration?.dimmed).toBe(true);
		expect(bobFlights[1].decoration?.dimmed).toBe(true);

		cardRegistry.flushImmediately();
		await Promise.resolve();

		expect(bus.getInFlightDrawCount("bob")).toBe(0);
		dispose();
	});

	it("initializes pendingLocalPlayDrawnId on mount if actionRequired is PlayDrawn", () => {
		storeAuth.username = "me";
		storeGame.actionRequired = Action.PlayDrawn;
		storeGame.actionContext = { card_id: 2 };
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
						{ id: 2, type: "red", value: "7" }
					]
				}
			],
			pending_draws: 0,
			draw_pile_size: 9
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

		expect(bus.pendingLocalPlayDrawnId).toBe(2);
		const flight = cardRegistry.activeFlights.find((f) => f.id === "2");
		expect(flight).toBeDefined();
		expect(flight!.pose.turned).toBe(false);
		const topPose = drawPileTopPose(placement, 10);
		expect(flight!.pose.x).toBeCloseTo(topPose[0]);
		expect(flight!.pose.y).toBeCloseTo(topPose[1] + DRAW_HOVER_LIFT);

		dispose();
	});
});
