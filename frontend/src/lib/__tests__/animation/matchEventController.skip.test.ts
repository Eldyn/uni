import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { createMatchEventBeatController } from "$components/game/animation/matchEventController.svelte";
import type { MatchEventBeat } from "$components/game/animation/baseBeats.svelte";
import type { CardBus } from "$components/game/card-bus.svelte";
import { storeGame } from "$stores/game.svelte";
import { storeAuth } from "$stores/auth.svelte";
import { storeTurnSkip } from "$stores/turnSkip.svelte";
import { storeAnimation } from "$stores/animation.svelte";
import type { BoardPlacement } from "$components/game/layout/boardPlacement";

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
	drawPileScale: 1
};

function fakeBus() {
	return {
		discardHistory: [] as never[],
		localHandSnapshot: { orderIds: [] as number[], scrollEm: 0, maxHalfSpanEm: 10 },
		previousLocalHandSnapshot: null,
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
		setPendingLocalPlayDrawnId: vi.fn(),
		setHoldingOpponents: vi.fn(),
		pendingLocalDragPlay: null,
		setPendingLocalDragPlay: vi.fn(),
		retainTopDiscard: vi.fn(),
		reshuffleDrawPileSize: null,
		onReshuffleCardLanding: null,
		getDrawPileHoverDipZ: () => 0
	} as unknown as CardBus;
}

function fakeRegistry(landing: Promise<void> = Promise.resolve()) {
	return {
		clearDecoration: vi.fn(),
		setDecoration: vi.fn(),
		seedPose: vi.fn(),
		registerCardMeta: vi.fn(),
		removeEntry: vi.fn(),
		enqueue: vi.fn().mockReturnValue(landing)
	} as unknown as import("$components/game/animation/cardRegistry.svelte").CardRegistry;
}

function stateWithTop(
	top: { id: number; type: string; value: string },
	active = "red",
	overrides: Record<string, unknown> = {}
) {
	return {
		active_type: active,
		current_turn: "bob",
		play_direction: 1,
		top_card: top,
		players: [
			{ username: "me", card_count: 4, is_bot: false, hand: [] },
			{ username: "bob", card_count: 3, is_bot: false, hand: [] }
		],
		pending_draws: 0,
		draw_pile_size: 10,
		...overrides
	} as never;
}

function harness(landing?: Promise<void>) {
	let beatHandler: ((beat: MatchEventBeat) => void) | null = null;
	let desyncHandler: (() => void) | null = null;
	const controller = createMatchEventBeatController({
		bus: fakeBus(),
		cardRegistry: fakeRegistry(landing),
		getPlacement: () => placement,
		getOpponentSeatAnchor: () => [0, 0, 0],
		subscribeBeats: (cb) => {
			beatHandler = cb;
			return () => {};
		},
		subscribeDesync: (cb) => {
			desyncHandler = cb;
			return () => {};
		}
	});
	return {
		controller,
		fire: (beat: MatchEventBeat) => beatHandler!(beat),
		desync: () => desyncHandler!()
	};
}

const turnBeat = (skipped: string[]): MatchEventBeat => ({
	seq: 1,
	kind: "turn",
	from: "me",
	to: "carol",
	direction: 1,
	deadlineMs: 1000,
	skipped
});

describe("matchEventController skip presentation", () => {
	beforeEach(() => {
		vi.useFakeTimers();
		storeAuth.username = "me";
		storeAnimation.enabled = true;
		storeGame.state = stateWithTop({ id: 2, type: "red", value: "7" });
		storeTurnSkip.reset();
	});

	afterEach(() => {
		storeTurnSkip.finish();
		storeGame.state = null;
		vi.clearAllTimers();
		vi.useRealTimers();
	});

	it("presents a later skip after an event whose turn advance never came", () => {
		const h = harness();

		h.fire({ seq: 1, kind: "skip", target: "bob", actor: "me" });
		storeTurnSkip.finish();
		h.controller.syncState();
		const tokenBefore = storeTurnSkip.token;
		h.fire(turnBeat(["bob"]));

		expect(storeTurnSkip.token).toBe(tokenBefore + 1);
		expect(storeTurnSkip.marks).toEqual(["bob"]);
	});

	it("does not re-stamp the marks when the turn advance follows the events", () => {
		const h = harness();

		h.fire({ seq: 1, kind: "skip", target: "bob", actor: "me" });
		h.fire({ seq: 2, kind: "skip", target: "carol", actor: "me" });
		const tokenBefore = storeTurnSkip.token;
		h.fire(turnBeat(["bob", "carol"]));

		expect(storeTurnSkip.token).toBe(tokenBefore);
		expect(storeTurnSkip.marks).toEqual(["bob", "carol"]);
		expect(storeTurnSkip.presentingTurn).toBe("me");
	});

	it("does not re-stamp the marks when the events follow the turn advance", () => {
		const h = harness();

		h.fire(turnBeat(["bob", "carol"]));
		const tokenBefore = storeTurnSkip.token;
		h.fire({ seq: 2, kind: "skip", target: "bob", actor: "me" });
		h.fire({ seq: 3, kind: "skip", target: "carol", actor: "me" });

		expect(storeTurnSkip.token).toBe(tokenBefore);
		expect(storeTurnSkip.marks).toEqual(["bob", "carol"]);
	});

	it("keeps one mark for a seat that two events name", () => {
		const h = harness();

		h.fire({ seq: 1, kind: "skip", target: "bob", actor: "me" });
		h.fire({ seq: 2, kind: "skip", target: "bob", actor: "me" });

		expect(storeTurnSkip.marks).toEqual(["bob"]);
	});
});
