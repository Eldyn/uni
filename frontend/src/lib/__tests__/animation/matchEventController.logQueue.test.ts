import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { createMatchEventBeatController } from "$components/game/animation/matchEventController.svelte";
import type { MatchEventBeat } from "$components/game/animation/baseBeats.svelte";
import type { CardBus } from "$components/game/card-bus.svelte";
import { storeGame } from "$stores/game.svelte";
import { storeAuth } from "$stores/auth.svelte";
import { storeLobby } from "$stores/lobby.svelte";
import { chatStore } from "$stores/chat.svelte";
import { storeAnimation } from "$stores/animation.svelte";
import { storeSpectator } from "$stores/spectator.svelte";
import { storeFxBudget } from "$stores/fxBudget.svelte";
import { createCardRegistry } from "$components/game/animation/cardRegistry.svelte";
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
	const abandonedListeners = new Set<() => void>();
	return {
		abandon: () => {
			for (const listener of abandonedListeners) listener();
		},
		onQueueAbandoned: (listener: () => void) => {
			abandonedListeners.add(listener);
			return () => abandonedListeners.delete(listener);
		},
		clearDecoration: vi.fn(),
		setDecoration: vi.fn(),
		seedPose: vi.fn(),
		registerCardMeta: vi.fn(),
		removeEntry: vi.fn(),
		enqueue: vi.fn().mockReturnValue(landing)
	} as unknown as import("$components/game/animation/cardRegistry.svelte").CardRegistry & {
		abandon: () => void;
	};
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

function harness(landing?: Promise<void>, registry = fakeRegistry(landing)) {
	let beatHandler: ((beat: MatchEventBeat) => void) | null = null;
	let desyncHandler: (() => void) | null = null;
	const controller = createMatchEventBeatController({
		bus: fakeBus(),
		cardRegistry: registry,
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
		registry,
		fire: (beat: MatchEventBeat) => beatHandler!(beat),
		desync: () => desyncHandler!()
	};
}

const skipTurn = (skipped: string[], direction = 1): MatchEventBeat => ({
	seq: 2,
	kind: "turn",
	from: "me",
	to: "carol",
	direction,
	deadlineMs: 1000,
	skipped
});

const play = (seq: number): MatchEventBeat => ({
	seq,
	kind: "play",
	player: "me",
	cardId: 2,
	auto: false
});

const partyKeys = () => chatStore.linesFor("party").map((line) => line.logKey);
const flush = () => new Promise((resolve) => setTimeout(resolve, 0));

describe("matchEventController log queue", () => {
	beforeEach(() => {
		chatStore.reset();
		storeLobby.current = {
			invite_code: "ABC123",
			members: []
		} as unknown as typeof storeLobby.current;
		storeAuth.username = "me";
		storeAnimation.enabled = false;
		storeGame.state = stateWithTop({ id: 2, type: "red", value: "7" });
	});

	afterEach(() => {
		storeGame.state = null;
		storeLobby.current = null;
		storeSpectator.reset();
		chatStore.reset();
	});

	it("holds the lines of a play while its card has not landed", async () => {
		const h = harness(new Promise<void>(() => undefined));
		h.fire(play(1));
		h.fire({ seq: 9, kind: "skip", target: "bob" });
		await flush();
		expect(partyKeys()).toEqual([]);
	});

	it("posts the held lines once, in order, when the animation queue abandons its work", async () => {
		const h = harness(new Promise<void>(() => undefined));
		h.fire(play(1));
		h.fire({ seq: 9, kind: "skip", target: "bob" });
		await flush();

		h.registry.abandon();
		await flush();
		h.registry.abandon();
		await flush();

		expect(partyKeys()).toEqual(["uni:log.play", "uni:log.skip"]);
	});

	it("keeps the lines of the events after an abandonment in event order", async () => {
		const h = harness(new Promise<void>(() => undefined));
		h.fire(play(1));
		h.registry.abandon();
		h.fire({ seq: 3, kind: "skip", target: "bob", actor: "me" });
		await flush();
		expect(partyKeys()).toEqual(["uni:log.play", "uni:log.skip"]);
	});

	it("stops listening to the animation queue when the controller is disposed", async () => {
		const h = harness(new Promise<void>(() => undefined));
		h.fire(play(1));
		h.controller.dispose();
		h.registry.abandon();
		await flush();
		expect(partyKeys()).toEqual([]);
	});

	describe("with the real animation queue", () => {
		it("posts every line once, in order, after a flush", async () => {
			const registry = createCardRegistry();
			const h = harness(undefined, registry as never);
			h.fire(play(1));
			h.fire(play(2));
			h.fire({ seq: 9, kind: "skip", target: "bob" });

			registry.flushImmediately();
			registry.flushImmediately();
			await flush();

			expect(partyKeys()).toEqual(["uni:log.play", "uni:log.play", "uni:log.skip"]);
		});

		it("posts every line once, in order, after a skip of the current beat", async () => {
			const registry = createCardRegistry();
			const h = harness(undefined, registry as never);
			h.fire(play(1));
			h.fire(play(2));
			h.fire({ seq: 9, kind: "skip", target: "bob" });

			registry.skipCurrent();
			registry.skipCurrent();
			registry.flushImmediately();
			await flush();

			expect(partyKeys()).toEqual(["uni:log.play", "uni:log.play", "uni:log.skip"]);
		});

		it("posts the lines of the batches that the cap drops", async () => {
			const previous = storeFxBudget.queueMax;
			storeFxBudget.queueMax = 2;
			const warn = vi.spyOn(console, "warn").mockImplementation(() => undefined);
			const registry = createCardRegistry();
			const h = harness(undefined, registry as never);
			const plays = 8;
			for (let index = 1; index <= plays; index += 1) h.fire(play(index));

			registry.flushImmediately();
			await flush();

			expect(partyKeys()).toEqual(Array.from({ length: plays }, () => "uni:log.play"));
			warn.mockRestore();
			storeFxBudget.queueMax = previous;
		});
	});
});
