import { describe, it, expect, vi, afterEach } from "vitest";
import { createMatchEventBeatController } from "$components/game/animation/matchEventController.svelte";
import type { MatchEventBeat } from "$components/game/animation/baseBeats.svelte";
import type { CardBus } from "$components/game/card-bus.svelte";
import { storeGame } from "$stores/game.svelte";
import { storeAuth } from "$stores/auth.svelte";
import { storeMatchIntro } from "$stores/matchIntro.svelte";
import { storeRenderSettings } from "$stores/renderSettings.svelte";
import type { BoardPlacement } from "$components/game/layout/boardPlacement";

const { startMatRipple, beginPending, clearPending } = vi.hoisted(() => ({
	startMatRipple: vi.fn(),
	beginPending: vi.fn(),
	clearPending: vi.fn()
}));

vi.mock("$components/game/three/ripple/matRipple.svelte", () => ({
	storeMatRipple: { startMatRipple, beginPending, clearPending },
	MAT_INITIAL_COLOR: "#663399"
}));

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
		setPendingLocalPlayDrawnId: vi.fn(),
		setHoldingOpponents: vi.fn(),
		retainTopDiscard: vi.fn()
	} as unknown as CardBus;
}

/** Fires each step's onComplete synchronously, standing in for the real
 *  CardRegistry's GSAP timeline — close enough to prove the controller wires
 *  the right ripple callback onto the discard-landing move step. */
function fakeRegistry() {
	return {
		clearDecoration: vi.fn(),
		setDecoration: vi.fn(),
		seedPose: vi.fn(),
		registerCardMeta: vi.fn(),
		removeEntry: vi.fn(),
		enqueue: vi.fn((beats: Array<Array<{ payload?: { onComplete?: () => void } }>>) => {
			for (const beat of beats) {
				for (const step of beat) {
					step.payload?.onComplete?.();
				}
			}
			return Promise.resolve();
		})
	} as unknown as import("$components/game/animation/cardRegistry.svelte").CardRegistry;
}

function baseState(overrides: Record<string, unknown> = {}) {
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
		draw_pile_size: 10,
		...overrides
	} as never;
}

function harness() {
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
		}
	});
	return {
		bus,
		cardRegistry,
		controller,
		fire: (beat: MatchEventBeat) => handler!(beat)
	};
}

describe("createMatchEventBeatController — mat ripple", () => {
	afterEach(() => {
		storeGame.state = null;
		storeAuth.username = "";
		storeMatchIntro.end();
		storeRenderSettings.matRipple = true;
		vi.restoreAllMocks();
		startMatRipple.mockClear();
	});

	it("starts a normal ripple when a non-wild card lands on the discard", () => {
		storeGame.state = baseState();
		storeAuth.username = "me";
		const h = harness();

		h.fire({ seq: 1, kind: "play", player: "me", cardId: 2, auto: false });

		expect(startMatRipple).toHaveBeenCalledWith(
			expect.any(String),
			"normal",
			expect.any(Object),
			expect.any(Number)
		);
	});

	it("starts a wild ripple immediately when the colour is already known at landing", () => {
		storeGame.state = baseState({
			top_card: { id: 3, type: "white", value: "wild" },
			active_type: "blue"
		});
		storeAuth.username = "me";
		const h = harness();

		h.fire({ seq: 1, kind: "play", player: "me", cardId: 3, auto: false });

		expect(startMatRipple).toHaveBeenCalledWith(
			expect.any(String),
			"wild",
			expect.any(Object),
			expect.any(Number)
		);
	});

	it("defers a wild ripple until syncState observes the chosen colour, firing exactly once", () => {
		storeGame.state = baseState({
			top_card: { id: 3, type: "white", value: "wild" },
			active_type: "white"
		});
		storeAuth.username = "me";
		const h = harness();

		h.fire({ seq: 1, kind: "play", player: "me", cardId: 3, auto: false });
		expect(startMatRipple).not.toHaveBeenCalled();

		// Colour still unresolved — syncState must not fire yet.
		h.controller.syncState();
		expect(startMatRipple).not.toHaveBeenCalled();

		// Colour resolves.
		storeGame.state = baseState({
			top_card: { id: 3, type: "white", value: "wild" },
			active_type: "green"
		});
		h.controller.syncState();
		expect(startMatRipple).toHaveBeenCalledTimes(1);
		expect(startMatRipple).toHaveBeenCalledWith(
			expect.any(String),
			"wild",
			expect.any(Object),
			expect.any(Number)
		);

		// Further syncState calls must not refire the same resolution.
		h.controller.syncState();
		expect(startMatRipple).toHaveBeenCalledTimes(1);
	});

	it("clears a stale pending wild ripple on a match reset, so it never fires", () => {
		storeGame.state = baseState({
			top_card: { id: 3, type: "white", value: "wild" },
			active_type: "white"
		});
		storeAuth.username = "me";
		const h = harness();

		h.fire({ seq: 1, kind: "play", player: "me", cardId: 3, auto: false });
		expect(startMatRipple).not.toHaveBeenCalled();

		// A new match starts — the coordinator's reset hook clears the stale
		// pending wild before it can ever resolve against the wrong geometry.
		h.controller.resetPendingWildRipple();

		storeGame.state = baseState({
			top_card: { id: 3, type: "white", value: "wild" },
			active_type: "green"
		});
		h.controller.syncState();

		expect(startMatRipple).not.toHaveBeenCalled();
	});
});
