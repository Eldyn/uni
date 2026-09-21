import { describe, it, expect, vi, afterEach } from "vitest";
import { createMatchEventBeatController } from "$components/game/animation/matchEventController.svelte";
import type { MatchEventBeat } from "$components/game/animation/baseBeats.svelte";
import type { CardBus } from "$components/game/card-bus.svelte";
import { storeGame } from "$stores/game.svelte";
import { storeAuth } from "$stores/auth.svelte";
import type { BoardPlacement } from "$components/game/layout/boardPlacement";
import type { AnimationBeat } from "$components/game/animation/types";

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

/** Minimal fake bus recording the calls the play path makes. */
function fakeBus() {
	return {
		discardHistory: [] as never[],
		localHandSnapshot: { orderIds: [] as number[], scrollEm: 0, maxHalfSpanEm: 10 },
		previousLocalHandSnapshot: null as null | {
			orderIds: number[];
			scrollEm: number;
			maxHalfSpanEm: number;
		},
		activeType: "white",
		setActiveType: vi.fn(),
		setDiscardTop: vi.fn(),
		addInFlightPlay: vi.fn(),
		removeInFlightPlay: vi.fn()
	} as unknown as CardBus;
}

/** Minimal fake registry recording the seeding + enqueue calls the play path makes. */
function fakeRegistry() {
	return {
		clearDecoration: vi.fn(),
		seedPose: vi.fn(),
		registerCardMeta: vi.fn(),
		enqueue: vi.fn().mockResolvedValue(undefined)
	} as unknown as import("$components/game/animation/cardRegistry.svelte").CardRegistry;
}

function baseState() {
	return {
		active_type: "red",
		current_turn: "me",
		play_direction: 1,
		top_card: { id: 2, type: "red", value: "7" },
		players: [
			{ username: "me", card_count: 4, is_bot: false, hand: [] },
			{ username: "bob", card_count: 3, is_bot: false, hand: [] }
		],
		pending_draws: 0,
		draw_pile_size: 10
	} as never;
}

function harness(
	overrides: Parameters<typeof createMatchEventBeatController>[0] extends infer D
		? Partial<D>
		: never = {}
) {
	let handler: ((beat: MatchEventBeat) => void) | null = null;
	const bus = fakeBus();
	const cardRegistry = fakeRegistry();
	const controller = createMatchEventBeatController({
		bus,
		cardRegistry,
		getPlacement: () => placement,
		getOpponentSeatAnchor: () => [0, 0, 0],
		subscribeBeats: (cb) => {
			handler = cb;
			return () => {
				handler = null;
			};
		},
		...overrides
	});
	return {
		bus,
		cardRegistry,
		controller,
		fire: (beat: MatchEventBeat) => handler!(beat),
		isSubscribed: () => handler !== null
	};
}

describe("createMatchEventBeatController", () => {
	afterEach(() => {
		storeGame.state = null;
		storeAuth.username = "";
		vi.restoreAllMocks();
	});

	it("subscribes for beats and dispose unsubscribes", () => {
		const h = harness();
		expect(h.isSubscribed()).toBe(true);
		h.controller.dispose();
		expect(h.isSubscribed()).toBe(false);
	});

	it("syncState mirrors active_type onto the bus", () => {
		storeGame.state = baseState();
		const h = harness();
		h.controller.syncState();
		expect(h.bus.setActiveType).toHaveBeenCalledWith("red");
	});

	it("syncState is a no-op when no state is hydrated", () => {
		const h = harness();
		h.controller.syncState();
		expect(h.bus.setActiveType).not.toHaveBeenCalled();
	});

	it("enqueues a single move beat for the local player's own play", () => {
		storeAuth.username = "me";
		storeGame.state = baseState();
		const h = harness();

		h.fire({ seq: 1, kind: "play", player: "me", cardId: 2, auto: false });

		expect(h.cardRegistry.enqueue).toHaveBeenCalledTimes(1);
		const [beats, resolveAnchor] = (h.cardRegistry.enqueue as ReturnType<typeof vi.fn>).mock
			.calls[0] as [AnimationBeat[], (name: string) => [number, number, number]];
		expect(beats).toHaveLength(2);
		expect(beats[0]!.map((s) => s.op)).toEqual(["move"]);
		expect(beats[0]![0]!.payload?.to).toBe("discard-pile");
		expect(beats[1]!.map((s) => s.op)).toEqual(["shake"]);
		expect(typeof resolveAnchor("discard-pile")).toBe("object");
	});

	it("enqueues move+flip for an opponent's play", () => {
		storeAuth.username = "me";
		storeGame.state = { ...baseState(), current_turn: "bob" } as never;
		const h = harness();

		h.fire({ seq: 2, kind: "play", player: "bob", cardId: 2, auto: false, fromZoneOrdinal: 1 });

		const [beats] = (h.cardRegistry.enqueue as ReturnType<typeof vi.fn>).mock.calls[0] as [
			AnimationBeat[]
		];
		expect(beats[0]!.map((s) => s.op)).toEqual(["move", "flip"]);
		const flip = beats[0]!.find((s) => s.op === "flip")!;
		expect(flip.payload?.turned).toBe(false);
		expect(flip.payload?.axis).toBe("y");
		expect(h.bus.addInFlightPlay).toHaveBeenCalledWith("bob");
	});

	it("seeds the opponent source slot from fromZoneOrdinal via getOpponentCardPose", () => {
		storeAuth.username = "me";
		storeGame.state = { ...baseState(), current_turn: "bob" } as never;
		const getOpponentCardPose = vi.fn(() => ({
			position: [1, 2, 3] as [number, number, number],
			spinDeg: 42
		}));
		const h = harness({ getOpponentCardPose });

		h.fire({ seq: 3, kind: "play", player: "bob", cardId: 2, auto: false, fromZoneOrdinal: 2 });

		// bob's post-play card_count is 3, so the pre-play hand had 4 cards —
		// from_zone_ordinal indexes into that PRE-play hand.
		expect(getOpponentCardPose).toHaveBeenCalledWith("bob", 4, 2);
		const [, pose] = (h.cardRegistry.seedPose as ReturnType<typeof vi.fn>).mock.calls[0] as [
			string,
			{ x: number; y: number; z: number; spinDeg: number }
		];
		expect([pose.x, pose.y, pose.z]).toEqual([1, 2, 3]);
		expect(pose.spinDeg).toBe(42);
	});

	it("sizes the opponent arc from the pre-play count for the last-slot case", () => {
		storeAuth.username = "me";
		storeGame.state = { ...baseState(), current_turn: "bob" } as never;
		const getOpponentCardPose = vi.fn(() => ({
			position: [0, 0, 0] as [number, number, number],
			spinDeg: 0
		}));
		const h = harness({ getOpponentCardPose });

		// Ordinal 3 only exists in the pre-play hand of 4, not the post-play 3.
		h.fire({ seq: 11, kind: "play", player: "bob", cardId: 2, auto: false, fromZoneOrdinal: 3 });

		expect(getOpponentCardPose).toHaveBeenCalledWith("bob", 4, 3);
	});

	it("falls back to the seat anchor when no fromZoneOrdinal is present", () => {
		storeAuth.username = "me";
		storeGame.state = { ...baseState(), current_turn: "bob" } as never;
		const getOpponentSeatAnchor = vi.fn(() => [7, 8, 9] as [number, number, number]);
		const h = harness({ getOpponentSeatAnchor });

		h.fire({ seq: 4, kind: "play", player: "bob", cardId: 2, auto: false });

		const [, pose] = (h.cardRegistry.seedPose as ReturnType<typeof vi.fn>).mock.calls[0] as [
			string,
			{ x: number; y: number; z: number }
		];
		expect([pose.x, pose.y, pose.z]).toEqual([7, 8, 9]);
	});

	it("ignores a play whose card id does not match the snapshot's top card", () => {
		storeAuth.username = "me";
		storeGame.state = baseState();
		const h = harness();

		h.fire({ seq: 5, kind: "play", player: "me", cardId: 99, auto: false });

		expect(h.cardRegistry.enqueue).not.toHaveBeenCalled();
		expect(h.cardRegistry.seedPose).not.toHaveBeenCalled();
	});

	it("seeds the local hand slot for the local player's own play", () => {
		storeAuth.username = "me";
		storeGame.state = baseState();
		const h = harness();
		(
			h.bus as {
				localHandSnapshot: { orderIds: number[]; scrollEm: number; maxHalfSpanEm: number };
			}
		).localHandSnapshot = { orderIds: [2, 5], scrollEm: 0, maxHalfSpanEm: 10 };

		h.fire({ seq: 6, kind: "play", player: "me", cardId: 2, auto: false });

		expect(h.cardRegistry.seedPose).toHaveBeenCalledWith(
			"2",
			expect.objectContaining({ scale: 1 })
		);
	});

	it("no-ops draw/reshuffle/turn/toast beats this slice", () => {
		storeAuth.username = "me";
		storeGame.state = baseState();
		const h = harness();

		h.fire({ seq: 7, kind: "draw", player: "me", count: 1, sourcePile: "draw", cardIds: [] });
		h.fire({ seq: 8, kind: "reshuffle", drawSize: 10, discardSize: 2 });
		h.fire({ seq: 9, kind: "turn", from: "me", to: "bob", direction: 1, deadlineMs: 1000 });
		h.fire({
			seq: 10,
			kind: "toast",
			target: "bob",
			statusKind: "frozen",
			magnitude: 1,
			durationUnit: "turns",
			instanceId: 1
		});

		expect(h.cardRegistry.enqueue).not.toHaveBeenCalled();
	});
});
