import { describe, it, expect, vi, afterEach } from "vitest";
import { createMatchEventBeatController } from "$components/game/animation/matchEventController.svelte";
import type { MatchEventBeat } from "$components/game/animation/baseBeats.svelte";
import type { CardBus } from "$components/game/card-bus.svelte";
import { storeGame } from "$stores/game.svelte";
import { storeAuth } from "$stores/auth.svelte";
import { storeAnimation } from "$stores/animation.svelte";
import { storeTurnCue } from "$stores/turnCue.svelte";
import { storeTurnSkip } from "$stores/turnSkip.svelte";
import { storeMatchIntro } from "$stores/matchIntro.svelte";
import type { BoardPlacement } from "$components/game/layout/boardPlacement";
import type { AnimationBeat } from "$components/game/animation/types";

const placement: BoardPlacement = {
	mat: {
		size: [10, 6],
		offsetX: 0,
		offsetZ: 0,
		quarterTurn: false,
		bounds: { left: -5, right: 5, far: -3, near: 3 }
	},
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
		removeInFlightPlay: vi.fn(),
		addInFlightDraw: vi.fn(),
		removeInFlightDraw: vi.fn(),
		addPendingLocalDraw: vi.fn(),
		removePendingLocalDraw: vi.fn(),
		pendingLocalDrawIds: new Set<number>(),
		pendingLocalPlayDrawnId: null,
		retainTopDiscard: vi.fn()
	} as unknown as CardBus;
}

/** Discard-history fixture — only the ids/type/value matter to the reshuffle path. */
function discardHistory(ids: number[]) {
	return ids.map((id, i) => ({
		card: { id, type: "red", value: String(id) },
		seq: i + 1,
		jitter: [0, 0] as [number, number],
		rotationDeg: 0
	}));
}

/** Minimal fake registry recording the seeding + enqueue calls the play path makes. */
function fakeRegistry() {
	return {
		clearDecoration: vi.fn(),
		setDecoration: vi.fn(),
		seedPose: vi.fn(),
		registerCardMeta: vi.fn(),
		removeEntry: vi.fn(),
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

/** Post-draw state: "me" holds [2,5,7,9] and the draw pile already shrank to 8
 *  (so the pre-draw pile the flight seeds from was 10). */
function localDrawState() {
	return {
		...baseState(),
		current_turn: "me",
		draw_pile_size: 8,
		players: [
			{
				username: "me",
				card_count: 4,
				is_bot: false,
				hand: [
					{ id: 2, type: "red", value: "7" },
					{ id: 5, type: "blue", value: "1" },
					{ id: 7, type: "green", value: "3" },
					{ id: 9, type: "yellow", value: "5" }
				]
			},
			{ username: "bob", card_count: 3, is_bot: false, hand: [] }
		]
	} as never;
}

/** Post-draw state for an opponent draw: bob holds one more card than the
 *  pre-draw count of 3, and the draw pile has already shrunk by `count`. */
function opponentDrawState(count: number, currentTurn = "bob") {
	return {
		...baseState(),
		current_turn: currentTurn,
		draw_pile_size: 10 - count,
		players: [
			{ username: "me", card_count: 4, is_bot: false, hand: [] },
			{ username: "bob", card_count: 3 + count, is_bot: false, hand: [] }
		]
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
		storeMatchIntro.end();
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

	it("syncState seeds the initial discard top when nothing is seeded yet", () => {
		storeGame.state = baseState();
		const h = harness();
		h.controller.syncState();
		expect(h.bus.setDiscardTop).toHaveBeenCalledTimes(1);
	});

	it("syncState leaves a live discard history untouched", () => {
		storeGame.state = baseState();
		const h = harness();
		(h.bus as { discardHistory: unknown[] }).discardHistory = discardHistory([9]);
		h.controller.syncState();
		expect(h.bus.setDiscardTop).not.toHaveBeenCalled();
	});

	it("defers the initial discard seed while the match intro is active", () => {
		storeGame.state = baseState();
		storeMatchIntro.active = true;
		const h = harness();

		h.controller.syncState();
		expect(h.bus.setDiscardTop).not.toHaveBeenCalled();

		storeMatchIntro.end();
		h.controller.syncState();
		expect(h.bus.setDiscardTop).toHaveBeenCalledTimes(1);
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

	it("enqueues staggered flip+move beats and seeds each id for a local draw", () => {
		storeAuth.username = "me";
		storeGame.state = localDrawState();
		const h = harness();

		h.fire({ seq: 20, kind: "draw", player: "me", count: 2, sourcePile: "draw", cardIds: [7, 9] });

		expect(h.cardRegistry.enqueue).toHaveBeenCalledTimes(1);
		const [beats, resolveAnchor] = (h.cardRegistry.enqueue as ReturnType<typeof vi.fn>).mock
			.calls[0] as [AnimationBeat[], (name: string) => [number, number, number]];
		expect(beats).toHaveLength(1);
		const steps = beats[0]!;
		expect(steps.map((s) => s.op)).toEqual(["flip", "move", "flip", "move"]);
		expect(steps[0]!.target).toBe("7");
		expect(steps[0]!.payload?.turned).toBe(false);
		expect(steps[1]!.target).toBe("7");
		expect(steps[1]!.payload?.to).toBe("local-draw-slot:7");
		expect(steps[3]!.target).toBe("9");
		expect(steps[3]!.payload?.to).toBe("local-draw-slot:9");

		// Each new id is seeded face-down at the draw pile and registered.
		expect(h.cardRegistry.seedPose).toHaveBeenCalledWith(
			"7",
			expect.objectContaining({ turned: true, flipDeg: 0 })
		);
		expect(h.cardRegistry.seedPose).toHaveBeenCalledWith(
			"9",
			expect.objectContaining({ turned: true, flipDeg: 0 })
		);
		expect(h.cardRegistry.registerCardMeta).toHaveBeenCalledWith("7", {
			type: "green",
			value: "3"
		});
		expect(h.cardRegistry.registerCardMeta).toHaveBeenCalledWith("9", {
			type: "yellow",
			value: "5"
		});

		// Both cards are hidden from LocalHand3D by pendingLocalDrawIds.
		expect(h.bus.addPendingLocalDraw).toHaveBeenCalledWith(7);
		expect(h.bus.addPendingLocalDraw).toHaveBeenCalledWith(9);

		// A per-card slot anchor exists for every slot key, and the shared
		// resolver returns a world-space tuple for each.
		expect(resolveAnchor("local-draw-slot:7")).toHaveLength(3);
		expect(resolveAnchor("local-draw-slot:9")).toHaveLength(3);

		// It is the local player's turn, so the flight is not dimmed.
		expect(h.cardRegistry.setDecoration).toHaveBeenCalledWith("7", { dimmed: false });
		expect(h.cardRegistry.setDecoration).toHaveBeenCalledWith("9", { dimmed: false });
	});

	it("clears every pending local draw in the enqueue finally", async () => {
		storeAuth.username = "me";
		storeGame.state = localDrawState();
		const h = harness();

		h.fire({ seq: 21, kind: "draw", player: "me", count: 2, sourcePile: "draw", cardIds: [7, 9] });

		// No step renderer runs against the fake registry, so no onCardComplete
		// fires and the finally block is the one that releases both cards.
		await vi.waitFor(() => {
			expect(h.bus.removePendingLocalDraw).toHaveBeenCalledWith(7);
			expect(h.bus.removePendingLocalDraw).toHaveBeenCalledWith(9);
		});
	});

	it("refreshes in-flight local draw dimming on syncState when the turn moves away", () => {
		storeAuth.username = "me";
		storeGame.state = localDrawState();
		const h = harness();

		h.fire({ seq: 22, kind: "draw", player: "me", count: 2, sourcePile: "draw", cardIds: [7, 9] });
		(h.cardRegistry.setDecoration as ReturnType<typeof vi.fn>).mockClear();

		storeGame.state = { ...localDrawState(), current_turn: "bob" } as never;
		h.controller.syncState();

		expect(h.cardRegistry.setDecoration).toHaveBeenCalledWith("7", { dimmed: true });
		expect(h.cardRegistry.setDecoration).toHaveBeenCalledWith("9", { dimmed: true });
	});

	it("enqueues synthetic staggered moves for an opponent draw", () => {
		storeAuth.username = "me";
		storeGame.state = opponentDrawState(2, "bob");
		const h = harness();

		h.fire({ seq: 23, kind: "draw", player: "bob", count: 2, sourcePile: "draw", cardIds: [] });

		expect(h.cardRegistry.enqueue).toHaveBeenCalledTimes(1);
		const [beats, resolveAnchor] = (h.cardRegistry.enqueue as ReturnType<typeof vi.fn>).mock
			.calls[0] as [AnimationBeat[], (name: string) => [number, number, number]];
		expect(beats).toHaveLength(1);
		const steps = beats[0]!;
		expect(steps.map((s) => s.op)).toEqual(["move", "move"]);

		const firstTarget = steps[0]!.target as string;
		const secondTarget = steps[1]!.target as string;
		expect(firstTarget).toMatch(/^draw:bob:\d+$/);
		expect(secondTarget).toMatch(/^draw:bob:\d+$/);
		expect(firstTarget).not.toBe(secondTarget);
		// No getOpponentCardPose provided, so both fall back to the seat anchor.
		expect(steps[0]!.payload?.to).toBe("seat:bob");
		expect(steps[1]!.payload?.to).toBe("seat:bob");
		// drawStaggerFor(2) === 0.1 — one step per card, offset.
		expect(steps[0]!.atS).toBe(0);
		expect(steps[1]!.atS).toBe(0.1);

		expect(h.cardRegistry.seedPose).toHaveBeenCalledWith(
			firstTarget,
			expect.objectContaining({ turned: true, flipDeg: 0 })
		);
		expect(h.bus.addInFlightDraw).toHaveBeenCalledWith("bob", 2);
		expect(h.bus.addPendingLocalDraw).not.toHaveBeenCalled();
		expect(resolveAnchor("seat:bob")).toHaveLength(3);
	});

	it("uses per-slot anchors and spin from getOpponentCardPose", () => {
		storeAuth.username = "me";
		storeGame.state = opponentDrawState(2, "bob");
		const getOpponentCardPose = vi.fn((_username: string, _count: number, slot: number) => ({
			position: [slot, slot, slot] as [number, number, number],
			spinDeg: slot * 10
		}));
		const h = harness({ getOpponentCardPose });

		h.fire({ seq: 24, kind: "draw", player: "bob", count: 2, sourcePile: "draw", cardIds: [] });

		// bob's post-draw count is 5, drawn 2 -> pre-draw arc held 3.
		expect(getOpponentCardPose).toHaveBeenCalledWith("bob", 4, 3);
		expect(getOpponentCardPose).toHaveBeenCalledWith("bob", 5, 4);

		const [beats] = (h.cardRegistry.enqueue as ReturnType<typeof vi.fn>).mock.calls[0] as [
			AnimationBeat[]
		];
		const steps = beats[0]!;
		expect(steps[0]!.payload?.to).toMatch(/^opponent-slot:bob:/);
		expect(steps[0]!.payload?.toSpinDeg).toBe(30);
		expect(steps[1]!.payload?.toSpinDeg).toBe(40);

		const resolver = (h.cardRegistry.enqueue as ReturnType<typeof vi.fn>).mock.calls[0]![1] as (
			name: string
		) => [number, number, number];
		expect(resolver(steps[0]!.payload?.to as string)).toEqual([3, 3, 3]);
	});

	it("dims opponent-draw cards when the opponent is not the current turn", () => {
		storeAuth.username = "me";
		storeGame.state = opponentDrawState(1, "me");
		const h = harness();

		h.fire({ seq: 25, kind: "draw", player: "bob", count: 1, sourcePile: "draw", cardIds: [] });

		const cardId = (h.cardRegistry.seedPose as ReturnType<typeof vi.fn>).mock
			.calls[0]![0] as string;
		expect(h.cardRegistry.setDecoration).toHaveBeenCalledWith(cardId, { dimmed: true });
	});

	it("does not dim opponent-draw cards on the opponent's own turn", () => {
		storeAuth.username = "me";
		storeGame.state = opponentDrawState(1, "bob");
		const h = harness();

		h.fire({ seq: 26, kind: "draw", player: "bob", count: 1, sourcePile: "draw", cardIds: [] });

		const cardId = (h.cardRegistry.seedPose as ReturnType<typeof vi.fn>).mock
			.calls[0]![0] as string;
		expect(h.cardRegistry.setDecoration).toHaveBeenCalledWith(cardId, { dimmed: false });
	});

	it("releases the in-flight opponent draw and synthetic entry in the finally", async () => {
		storeAuth.username = "me";
		storeGame.state = opponentDrawState(2, "bob");
		const h = harness();

		h.fire({ seq: 27, kind: "draw", player: "bob", count: 2, sourcePile: "draw", cardIds: [] });
		const firstTarget = (h.cardRegistry.seedPose as ReturnType<typeof vi.fn>).mock
			.calls[0]![0] as string;

		await vi.waitFor(() => {
			expect(h.bus.removeInFlightDraw).toHaveBeenCalledWith("bob", 2);
			expect(h.cardRegistry.removeEntry).toHaveBeenCalledWith(firstTarget);
		});
	});

	it("refreshes in-flight opponent draw dimming on syncState when the turn moves away", () => {
		storeAuth.username = "me";
		storeGame.state = opponentDrawState(1, "bob");
		const h = harness();

		h.fire({ seq: 28, kind: "draw", player: "bob", count: 1, sourcePile: "draw", cardIds: [] });
		const cardId = (h.cardRegistry.seedPose as ReturnType<typeof vi.fn>).mock
			.calls[0]![0] as string;
		(h.cardRegistry.setDecoration as ReturnType<typeof vi.fn>).mockClear();

		storeGame.state = { ...opponentDrawState(1, "bob"), current_turn: "me" } as never;
		h.controller.syncState();

		expect(h.cardRegistry.setDecoration).toHaveBeenCalledWith(cardId, { dimmed: true });
	});

	it("enqueues move+flip for the discard history and retains the top discard on reshuffle", async () => {
		storeAuth.username = "me";
		storeGame.state = baseState();
		const h = harness();
		(h.bus as { discardHistory: unknown[] }).discardHistory = discardHistory([1, 2, 3]);

		// Seed the pre-reshuffle pile size the packet diff is computed against.
		h.controller.syncState();
		h.fire({ seq: 30, kind: "reshuffle", drawSize: 12, discardSize: 3 });

		expect(h.cardRegistry.enqueue).toHaveBeenCalledTimes(1);
		const [beats] = (h.cardRegistry.enqueue as ReturnType<typeof vi.fn>).mock.calls[0] as [
			AnimationBeat[]
		];
		expect(beats).toHaveLength(1);
		expect(beats[0]!.map((s) => s.op)).toEqual(["move", "flip", "move", "flip"]);
		// Only the two cards below the retained top fly back.
		expect(beats[0]!.filter((s) => s.op === "move").map((s) => s.target)).toEqual(["1", "2"]);

		expect(h.cardRegistry.registerCardMeta).toHaveBeenCalledWith("1", expect.objectContaining({}));
		expect(h.cardRegistry.seedPose).toHaveBeenCalledWith(
			"1",
			expect.objectContaining({ turned: false, scale: 1 })
		);

		await vi.waitFor(() => {
			expect(h.bus.retainTopDiscard).toHaveBeenCalledWith(2);
		});
		await vi.waitFor(() => {
			expect((h.bus as { reshuffleDrawPileSize: number | null }).reshuffleDrawPileSize).toBeNull();
		});
		expect(h.cardRegistry.removeEntry).toHaveBeenCalledWith("1");
		expect(h.cardRegistry.removeEntry).toHaveBeenCalledWith("2");
	});

	it("is a no-op when the draw pile does not grow", () => {
		storeAuth.username = "me";
		storeGame.state = baseState();
		const h = harness();

		h.controller.syncState();
		h.fire({ seq: 31, kind: "reshuffle", drawSize: 10, discardSize: 3 });
		h.fire({ seq: 32, kind: "reshuffle", drawSize: 9, discardSize: 3 });

		expect(h.cardRegistry.enqueue).not.toHaveBeenCalled();
		expect(h.bus.retainTopDiscard).not.toHaveBeenCalled();
	});

	it("is a no-op when the previous pile size has not been seeded", () => {
		storeAuth.username = "me";
		storeGame.state = baseState();
		const h = harness();

		h.fire({ seq: 33, kind: "reshuffle", drawSize: 12, discardSize: 3 });

		expect(h.cardRegistry.enqueue).not.toHaveBeenCalled();
		expect(h.bus.retainTopDiscard).not.toHaveBeenCalled();
	});

	it("synthesizes the backfill when discard history is shorter than the amount reshuffled", async () => {
		storeAuth.username = "me";
		storeGame.state = baseState();
		const h = harness();
		// Only the top discard exists client-side (e.g. after a refresh).
		(h.bus as { discardHistory: unknown[] }).discardHistory = discardHistory([3]);

		h.controller.syncState();
		h.fire({ seq: 34, kind: "reshuffle", drawSize: 12, discardSize: 1 });

		const [beats] = (h.cardRegistry.enqueue as ReturnType<typeof vi.fn>).mock.calls[0] as [
			AnimationBeat[]
		];
		const moveSteps = beats[0]!.filter((s) => s.op === "move");
		expect(moveSteps.map((s) => s.target)).toEqual(["-1000", "-1001"]);
		expect(h.cardRegistry.registerCardMeta).toHaveBeenCalledWith("-1000", {
			id: -1000,
			type: "wild",
			value: "0"
		});

		// Landing callbacks drive the live draw-pile size + landing ping off the
		// pre-reshuffle base.
		(moveSteps[0]!.payload?.onComplete as () => void)();
		expect((h.bus as { reshuffleDrawPileSize: number | null }).reshuffleDrawPileSize).toBe(11);
		expect(
			(h.bus as { onReshuffleCardLanding: { index: number; total: number } | null })
				.onReshuffleCardLanding
		).toMatchObject({ index: 0, total: 2 });

		await vi.waitFor(() => {
			expect(h.bus.retainTopDiscard).toHaveBeenCalledWith(0);
			expect((h.bus as { onReshuffleCardLanding: unknown }).onReshuffleCardLanding).toBeNull();
		});
	});

	it("no-ops an empty-id draw and reshuffle/turn/toast beats", () => {
		storeAuth.username = "me";
		storeGame.state = baseState();
		const h = harness();

		h.fire({ seq: 7, kind: "draw", player: "me", count: 1, sourcePile: "draw", cardIds: [] });
		h.fire({ seq: 8, kind: "reshuffle", drawSize: 10, discardSize: 2 });
		h.fire({
			seq: 9,
			kind: "turn",
			from: "me",
			to: "bob",
			direction: 1,
			deadlineMs: 1000,
			skipped: []
		});
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

	it("stamps an X over each skipped seat and gates the turn", () => {
		storeAuth.username = "me";
		storeGame.state = baseState();
		storeAnimation.enabled = true;
		const h = harness();

		h.fire({
			seq: 11,
			kind: "turn",
			from: "me",
			to: "carol",
			direction: 1,
			deadlineMs: 1000,
			skipped: ["bob"]
		});

		expect(storeTurnSkip.marks).toEqual(["bob"]);
		expect(storeTurnSkip.presentingTurn).toBe("me");
		storeTurnSkip.finish();
		expect(storeTurnSkip.marks).toEqual([]);
		expect(storeTurnSkip.presentingTurn).toBeNull();
	});

	it("raises the turn cue only when the beat hands the turn to the local player", () => {
		storeAuth.username = "me";
		storeGame.state = baseState();
		const h = harness();
		const token = storeTurnCue.token;

		h.fire({
			seq: 20,
			kind: "turn",
			from: "me",
			to: "bob",
			direction: 1,
			deadlineMs: 1,
			skipped: []
		});
		expect(storeTurnCue.token).toBe(token);

		h.fire({
			seq: 21,
			kind: "turn",
			from: "bob",
			to: "me",
			direction: 1,
			deadlineMs: 1,
			skipped: []
		});
		expect(storeTurnCue.token).toBe(token + 1);
	});

	it("does not present a skip when the turn carries none", () => {
		storeAuth.username = "me";
		storeGame.state = baseState();
		storeAnimation.enabled = true;
		const h = harness();

		h.fire({
			seq: 12,
			kind: "turn",
			from: "me",
			to: "bob",
			direction: 1,
			deadlineMs: 1000,
			skipped: []
		});

		expect(storeTurnSkip.marks).toEqual([]);
		expect(storeTurnSkip.presentingTurn).toBeNull();
	});
});
