import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { createMatchEventBeatController } from "$components/game/animation/matchEventController.svelte";
import type { MatchEventBeat } from "$components/game/animation/baseBeats.svelte";
import type { CardBus } from "$components/game/card-bus.svelte";
import { storeGame } from "$stores/game.svelte";
import { storeAuth } from "$stores/auth.svelte";
import { storeMatchIntro } from "$stores/matchIntro.svelte";
import { storeRenderSettings } from "$stores/renderSettings.svelte";
import { CARD_COLOR_MAP } from "$lib/palette";
import { storeMatRipple } from "$components/game/three/ripple/matRipple.svelte";
import type { BoardPlacement } from "$components/game/layout/boardPlacement";

const RED = CARD_COLOR_MAP["red"];
const BLUE = CARD_COLOR_MAP["blue"];

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
		retainTopDiscard: vi.fn()
	} as unknown as CardBus;
}

/** Collects the landing callbacks without firing them, so the test can hold
 *  the card "in flight" and simulate Playmat3D's active_type effect between
 *  the snapshot and the landing — the real ordering that hid the ripple. */
function deferredRegistry() {
	const completes: Array<() => void> = [];
	const registry = {
		clearDecoration: vi.fn(),
		setDecoration: vi.fn(),
		seedPose: vi.fn(),
		registerCardMeta: vi.fn(),
		removeEntry: vi.fn(),
		enqueue: vi.fn((beats: Array<Array<{ payload?: { onComplete?: () => void } }>>) => {
			for (const beat of beats) {
				for (const step of beat) {
					if (typeof step.payload?.onComplete === "function") {
						completes.push(step.payload.onComplete);
					}
				}
			}
			return Promise.resolve();
		})
	} as unknown as import("$components/game/animation/cardRegistry.svelte").CardRegistry;
	return {
		registry,
		land: () => {
			const callbacks = completes.splice(0);
			for (const callback of callbacks) callback();
		}
	};
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
	const { registry, land } = deferredRegistry();
	const controller = createMatchEventBeatController({
		bus,
		cardRegistry: registry,
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
		controller,
		land,
		fire: (beat: MatchEventBeat) => handler!(beat)
	};
}

describe("matchEventController — mat ripple ordering", () => {
	beforeEach(() => {
		vi.useFakeTimers();
		storeRenderSettings.matRipple = true;
		storeMatRipple.active = false;
		storeMatRipple.committedColor = RED;
		storeMatRipple.fromColor = RED;
		storeMatRipple.toColor = RED;
		storeMatRipple.clearPending();
	});

	afterEach(() => {
		vi.clearAllTimers();
		vi.useRealTimers();
		storeGame.state = null;
		storeAuth.username = "";
		storeMatchIntro.end();
		storeRenderSettings.matRipple = true;
		vi.restoreAllMocks();
	});

	it("holds the pre-play colour from the snapshot until the card lands, then ripples from it", () => {
		storeGame.state = baseState({
			active_type: "blue",
			top_card: { id: 2, type: "blue", value: "7" }
		});
		storeAuth.username = "me";
		const h = harness();

		// 1) Snapshot arrives; the controller processes the play synchronously
		//    while the card is still in flight. This is the moment active_type
		//    has just changed to blue.
		h.fire({ seq: 1, kind: "play", player: "me", cardId: 2, auto: false });

		// 2) Playmat3D's active_type effect now tries to adopt the new colour —
		//    hundreds of ms before the flight completes.
		storeMatRipple.syncColor(BLUE);

		// The hold: the mat must still show the pre-play colour.
		expect(storeMatRipple.committedColor).toBe(RED);

		// 3) The card lands and the sweep starts.
		h.land();

		expect(storeMatRipple.fromColor).toBe(RED);
		expect(storeMatRipple.toColor).toBe(BLUE);
		expect(storeMatRipple.fromColor).not.toBe(storeMatRipple.toColor);

		// After the sweep finishes the mat is blue.
		vi.advanceTimersByTime(storeMatRipple.durationMs);
		expect(storeMatRipple.committedColor).toBe(BLUE);
	});

	it("holds the pre-play colour through a deferred-wild resolution before it ripples", () => {
		storeGame.state = baseState({
			active_type: "white",
			top_card: { id: 3, type: "white", value: "wild" }
		});
		storeAuth.username = "me";
		const h = harness();

		h.fire({ seq: 1, kind: "play", player: "me", cardId: 3, auto: false });

		// active_type still reads "white" — the mat keeps its pre-play colour.
		storeMatRipple.syncColor(RED);
		expect(storeMatRipple.committedColor).toBe(RED);

		// The colour pick lands in a later snapshot; Playmat3D's effect may run
		// before or after the controller's syncState. Whichever order, the hold
		// must survive and the sweep must start from the held colour.
		storeGame.state = baseState({
			active_type: "green",
			top_card: { id: 3, type: "white", value: "wild" }
		});
		h.controller.syncState();
		storeMatRipple.syncColor(CARD_COLOR_MAP["green"]);

		expect(storeMatRipple.fromColor).toBe(RED);
		expect(storeMatRipple.toColor).toBe(CARD_COLOR_MAP["green"]);
		expect(storeMatRipple.fromColor).not.toBe(storeMatRipple.toColor);
	});
});
