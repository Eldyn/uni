import { describe, it, expect, vi, afterEach } from "vitest";
import { createMatchIntroController } from "$components/game/animation/matchIntroController.svelte";
import { localHandSlotAnchor } from "$components/game/animation/baseBeats.svelte";
import type { CardBus } from "$components/game/card-bus.svelte";
import type { CardRegistry } from "$components/game/animation/cardRegistry.svelte";
import type { BoardPlacement } from "$components/game/layout/boardPlacement";
import type { AnimationBeat } from "$components/game/animation/types";
import { storeGame } from "$stores/game.svelte";
import { storeAuth } from "$stores/auth.svelte";
import { storeAnimation } from "$stores/animation.svelte";
import { storeMatchIntro } from "$stores/matchIntro.svelte";

const placement: BoardPlacement = {
	mat: {} as never,
	handScale: 1,
	centerScale: 1,
	discardX: 3,
	discardZ: 0,
	localSeatZ: 5,
	localAvatarZ: 6,
	drawPileX: -3,
	drawPileZ: 0,
	drawPileScale: 1
};

interface FakeDiscardEntry {
	card: { id: number; type: string; value: string };
	seq: number;
	jitter: [number, number];
	rotationDeg: number;
}

/** Minimal fake bus: real `setDiscardTop` history-append so skip's "ends with
 *  the top card" assertion is meaningful, spies for the rest. */
function fakeBus() {
	const bus = {
		discardHistory: [] as FakeDiscardEntry[],
		localHandSnapshot: { orderIds: [] as number[], scrollEm: 0, maxHalfSpanEm: 10 },
		addInFlightDraw: vi.fn(),
		removeInFlightDraw: vi.fn(),
		addPendingLocalDraw: vi.fn(),
		removePendingLocalDraw: vi.fn(),
		setDiscardTop: vi.fn((card: { id: number; type: string; value: string } | null) => {
			if (!card) return;
			bus.discardHistory = [
				...bus.discardHistory,
				{ card, seq: bus.discardHistory.length + 1, jitter: [0, 0], rotationDeg: 0 }
			];
		})
	};
	return bus;
}

/** With `resolveEnqueue: false` (default) the enqueue promise never resolves, so
 *  `start` stays parked at the deal beat — letting a test observe the begun
 *  state without racing the relocate/final-discard phases. `true` lets the
 *  whole cinematic run to completion (with a high speed multiplier). */
function fakeRegistry(resolveEnqueue = false) {
	return {
		seedPose: vi.fn(),
		registerCardMeta: vi.fn(),
		removeEntry: vi.fn(),
		clearDecoration: vi.fn(),
		enqueue: vi.fn(() => (resolveEnqueue ? Promise.resolve() : new Promise<void>(() => {}))),
		flushImmediately: vi.fn()
	};
}

/** 2-card local hand on a 2-seat table; deal order is ["bob", "me"] and the
 *  centre pile starts at 30 + 2*2 + 1 = 35. */
function baseState() {
	return {
		active_type: "red",
		current_turn: "me",
		play_direction: 1,
		top_card: { id: 2, type: "red", value: "7" },
		players: [
			{
				username: "me",
				card_count: 2,
				is_bot: false,
				hand: [
					{ id: 10, type: "red", value: "3" },
					{ id: 11, type: "blue", value: "5" }
				]
			},
			{ username: "bob", card_count: 2, is_bot: true }
		],
		pending_draws: 0,
		draw_pile_size: 30
	} as never;
}

function harness({ resolveEnqueue = false } = {}) {
	const bus = fakeBus();
	const cardRegistry = fakeRegistry(resolveEnqueue);
	const intro = createMatchIntroController({
		bus: bus as unknown as CardBus,
		cardRegistry: cardRegistry as unknown as CardRegistry,
		getPlacement: () => placement,
		getOpponentCardScale: () => 0.5,
		getOpponentCardPose: (_username, _count, slotIndex) => ({
			position: [slotIndex, 0, 0],
			spinDeg: slotIndex * 5
		}),
		getOpponentSeatRotationDeg: () => 0
	});
	return { bus, cardRegistry, intro };
}

describe("createMatchIntroController", () => {
	afterEach(() => {
		storeGame.state = null;
		storeAuth.username = "";
		storeAnimation.enabled = true;
		storeAnimation.speedMultiplier = 1;
		storeMatchIntro.end();
		vi.restoreAllMocks();
	});

	it("is a no-op without players", async () => {
		const h = harness();
		await h.intro.start({ players: [] } as never);
		expect(storeMatchIntro.active).toBe(false);
		expect(h.cardRegistry.enqueue).not.toHaveBeenCalled();
	});

	it("with animations disabled jumps straight to the finished state", async () => {
		storeAuth.username = "me";
		storeGame.state = baseState();
		storeAnimation.enabled = false;
		const h = harness();

		await h.intro.start(baseState());

		expect(storeMatchIntro.active).toBe(false);
		expect(storeMatchIntro.discardHidden).toBe(false);
		expect(storeMatchIntro.forcePurpleMat).toBe(false);
		expect(storeMatchIntro.drawPileCount).toBeNull();
		expect(storeMatchIntro.drawPilePos).toBeNull();

		// The top card is seeded and no cinematic flags/beats were left on.
		expect(h.bus.setDiscardTop).toHaveBeenCalledTimes(1);
		expect((h.bus.setDiscardTop as ReturnType<typeof vi.fn>).mock.calls[0]![0]).toMatchObject({
			id: 2
		});
		expect(h.cardRegistry.enqueue).not.toHaveBeenCalled();
		expect(h.bus.addInFlightDraw).not.toHaveBeenCalled();
		expect(h.bus.addPendingLocalDraw).not.toHaveBeenCalled();
	});

	it("begins the intro, overrides the centre pile and enqueues one deal beat", async () => {
		storeAuth.username = "me";
		storeGame.state = baseState();
		storeAnimation.enabled = true;
		const h = harness();

		void h.intro.start(baseState());
		await vi.waitFor(() => expect(h.cardRegistry.enqueue).toHaveBeenCalledTimes(1));

		expect(storeMatchIntro.active).toBe(true);
		expect(storeMatchIntro.discardHidden).toBe(true);
		expect(storeMatchIntro.forcePurpleMat).toBe(true);
		expect(storeMatchIntro.drawPilePos).toEqual({ x: 0, z: 0 });
		expect(storeMatchIntro.drawPileCount).toBe(35);

		// Exactly one beat, carrying both seats' cards (locals flip+move).
		const [beats, resolver] = (h.cardRegistry.enqueue as ReturnType<typeof vi.fn>).mock
			.calls[0] as [AnimationBeat[], (name: string) => [number, number, number]];
		expect(beats).toHaveLength(1);
		const cardIds = beats[0]!.filter((s) => s.op === "move").map((s) => s.target);
		expect(cardIds).toEqual(["intro:bob:0", "10", "intro:bob:1", "11"]);
		// Every seed happened before the enqueue.
		expect(h.cardRegistry.seedPose).toHaveBeenCalledWith(
			"10",
			expect.objectContaining({ turned: true, scale: 1 })
		);
		expect(h.cardRegistry.registerCardMeta).toHaveBeenCalledWith("10", {
			type: "red",
			value: "3"
		});

		// The local row grows one reveal per landing: round r targets a row of
		// r + 1 cards, NOT the final 2-card row (which would snap on landing).
		expect(resolver("intro-local:10")).toEqual(
			localHandSlotAnchor(1, 0, placement, h.bus.localHandSnapshot)
		);
		expect(resolver("intro-local:11")).toEqual(
			localHandSlotAnchor(2, 1, placement, h.bus.localHandSnapshot)
		);

		// Suppression: bob's dealt cards are hidden, both local ids are pending.
		expect(h.bus.addInFlightDraw).toHaveBeenCalledWith("bob", 2);
		expect(h.bus.addPendingLocalDraw).toHaveBeenCalledWith(10);
		expect(h.bus.addPendingLocalDraw).toHaveBeenCalledWith(11);

		h.intro.skip();
	});

	it("runs the relocate and final-discard phases to completion", async () => {
		storeAuth.username = "me";
		storeGame.state = baseState();
		storeAnimation.enabled = true;
		storeAnimation.speedMultiplier = 100;
		const h = harness({ resolveEnqueue: true });

		await h.intro.start(baseState());

		// Deal + final discard, each one beat.
		expect(h.cardRegistry.enqueue).toHaveBeenCalledTimes(2);
		const discardBeats = (h.cardRegistry.enqueue as ReturnType<typeof vi.fn>).mock
			.calls[1]![0] as AnimationBeat[];
		expect(discardBeats).toHaveLength(1);
		expect(discardBeats[0]!.map((s) => s.op)).toEqual(["flip", "move"]);
		expect(discardBeats[0]!.find((s) => s.op === "move")!.payload?.to).toBe("discard-pile");

		expect(h.bus.setDiscardTop).toHaveBeenCalledTimes(1);
		expect((h.bus.setDiscardTop as ReturnType<typeof vi.fn>).mock.calls[0]![0]).toMatchObject({
			id: 2
		});
		expect(storeMatchIntro.active).toBe(false);
		expect(storeMatchIntro.discardHidden).toBe(false);
		expect(storeMatchIntro.forcePurpleMat).toBe(false);
		expect(storeMatchIntro.drawPileCount).toBeNull();
		expect(storeMatchIntro.drawPilePos).toBeNull();
	});

	it("skip is idempotent and ends the intro with the top card on the discard", async () => {
		storeAuth.username = "me";
		storeGame.state = baseState();
		storeAnimation.enabled = true;
		const h = harness();

		void h.intro.start(baseState());
		await vi.waitFor(() => expect(h.cardRegistry.enqueue).toHaveBeenCalled());

		h.intro.skip();
		h.intro.skip();

		expect(storeMatchIntro.active).toBe(false);
		expect(h.bus.setDiscardTop).toHaveBeenCalledTimes(1);
		expect(h.bus.discardHistory.at(-1)?.card.id).toBe(2);
		expect(h.bus.removePendingLocalDraw).toHaveBeenCalledWith(10);
		expect(h.bus.removePendingLocalDraw).toHaveBeenCalledWith(11);
		expect(h.bus.removeInFlightDraw).toHaveBeenCalledWith("bob", 2);
		expect(h.cardRegistry.removeEntry).toHaveBeenCalledWith("intro:bob:0");
		expect(h.cardRegistry.flushImmediately).toHaveBeenCalledTimes(1);
	});

	it("does not touch the beat queue when skipped or disposed before start", () => {
		const skipped = harness();
		skipped.intro.skip();
		expect(skipped.cardRegistry.flushImmediately).not.toHaveBeenCalled();

		const disposed = harness();
		disposed.intro.dispose();
		expect(disposed.cardRegistry.flushImmediately).not.toHaveBeenCalled();
	});

	it("ignores a second start while the first is still running", async () => {
		storeAuth.username = "me";
		storeGame.state = baseState();
		storeAnimation.enabled = true;
		const h = harness();

		void h.intro.start(baseState());
		await vi.waitFor(() => expect(h.cardRegistry.enqueue).toHaveBeenCalledTimes(1));

		// `running` is still true (the deal promise never resolves), so the
		// re-entrant call returns without re-beginning over the live state.
		void h.intro.start(baseState());
		expect(h.cardRegistry.enqueue).toHaveBeenCalledTimes(1);
		expect(storeMatchIntro.active).toBe(true);
		expect(storeMatchIntro.drawPilePos).toEqual({ x: 0, z: 0 });

		h.intro.skip();
	});
});
