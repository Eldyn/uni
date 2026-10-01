import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { gsap } from "gsap";
import { createMatchEventBeatController } from "$components/game/animation/matchEventController.svelte";
import type { MatchEventBeat } from "$components/game/animation/baseBeats.svelte";
import { CardRegistry } from "$components/game/animation/cardRegistry.svelte";
import type { AnimationBeat } from "$components/game/animation/types";
import { impactProfileFor } from "$components/game/animation/impactProfile";
import { ZERO_CAMERA_OFFSET } from "$components/game/layout/cameraRig";
import type { BoardPlacement } from "$components/game/layout/boardPlacement";
import type { CardBus } from "$components/game/card-bus.svelte";
import { storeGame } from "$stores/game.svelte";
import { storeAuth } from "$stores/auth.svelte";
import { storeAnimation } from "$stores/animation.svelte";
import { storeWebglCapability } from "$stores/webglCapability.svelte";
import { storeCameraOffset } from "$stores/cameraOffset.svelte";

const USER_SPEED = 2;

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
} as BoardPlacement;

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
		pendingLocalDrawIds: new Set<number>(),
		pendingLocalPlayDrawnId: null,
		pendingLocalDragPlay: null,
		setPendingLocalDragPlay: vi.fn()
	} as unknown as CardBus;
}

function fakeRegistry() {
	return {
		clearDecoration: vi.fn(),
		setDecoration: vi.fn(),
		seedPose: vi.fn(),
		registerCardMeta: vi.fn(),
		removeEntry: vi.fn(),
		enqueue: vi.fn().mockResolvedValue(undefined)
	} as unknown as CardRegistry;
}

function stateWithTop(top: { id: number; type: string; value: string }, activeType = top.type) {
	return {
		active_type: activeType,
		current_turn: "me",
		play_direction: 1,
		top_card: top,
		players: [
			{ username: "me", card_count: 4, is_bot: false, hand: [] },
			{ username: "bob", card_count: 3, is_bot: false, hand: [] }
		],
		pending_draws: 0,
		draw_pile_size: 10
	} as never;
}

function harness(cardRegistry: CardRegistry) {
	let handler: ((beat: MatchEventBeat) => void) | null = null;
	createMatchEventBeatController({
		bus: fakeBus(),
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
	return (beat: MatchEventBeat) => handler!(beat);
}

function landingBeatFor(top: { id: number; type: string; value: string }): AnimationBeat {
	storeGame.state = stateWithTop(top);
	const registry = fakeRegistry();
	harness(registry)({ seq: 1, kind: "play", player: "bob", cardId: top.id, auto: false });
	const [beats] = (registry.enqueue as ReturnType<typeof vi.fn>).mock.calls[0] as [AnimationBeat[]];
	return beats[1]!;
}

describe("matchEventController — special-card impact", () => {
	beforeEach(() => {
		storeAuth.username = "me";
		storeAnimation.enabled = true;
		storeAnimation.speedMultiplier = USER_SPEED;
		storeWebglCapability.reducedMotion = false;
		storeCameraOffset.clearOffset();
	});

	afterEach(() => {
		storeGame.state = null;
		storeAuth.username = "";
		storeAnimation.speedMultiplier = 1;
		storeWebglCapability.reducedMotion = false;
		storeCameraOffset.clearOffset();
		vi.restoreAllMocks();
	});

	it("adds the impact to the landing shake of a special card", () => {
		const top = { id: 4, type: "green", value: "+2" };
		const beat = landingBeatFor(top);
		expect(beat.map((step) => step.op)).toEqual(["shake", "impact"]);
		expect(beat[1]!.payload?.profile).toEqual(impactProfileFor(top));
	});

	it("keeps a plain number card's landing to the shake alone", () => {
		expect(landingBeatFor({ id: 2, type: "red", value: "7" }).map((step) => step.op)).toEqual([
			"shake"
		]);
	});

	it("drops the impact under reduced motion", () => {
		storeWebglCapability.reducedMotion = true;
		const beat = landingBeatFor({ id: 5, type: "white", value: "jolly_draw4" });
		expect(beat.map((step) => step.op)).toEqual(["shake"]);
	});

	it("drops the impact when animations are off", () => {
		storeAnimation.enabled = false;
		const beat = landingBeatFor({ id: 5, type: "white", value: "jolly_draw4" });
		expect(beat.map((step) => step.op)).toEqual(["shake"]);
	});

	it("leaves speed and camera at rest after a skip-forward through a real queue", async () => {
		gsap.ticker.lagSmoothing(false);
		const applySpy = vi.spyOn(storeCameraOffset, "applyOffset");
		const createTimeline = gsap.timeline.bind(gsap);
		const beatTimelines: gsap.core.Timeline[] = [];
		const registry = new CardRegistry();
		const top = { id: 6, type: "white", value: "jolly_draw4" };
		storeGame.state = stateWithTop(top, "red");
		vi.spyOn(gsap, "timeline").mockImplementation((vars) => {
			const timeline = createTimeline(vars);
			beatTimelines.push(timeline);
			return timeline;
		});

		harness(registry)({ seq: 1, kind: "play", player: "bob", cardId: top.id, auto: false });
		registry.flushImmediately();
		gsap.updateRoot(gsap.globalTimeline.time() + 1);

		const queueTimelines = beatTimelines.filter((timeline) => timeline.timeScale() !== 1);
		expect(queueTimelines.length).toBeGreaterThanOrEqual(2);
		for (const timeline of queueTimelines) expect(timeline.timeScale()).toBe(USER_SPEED);
		expect(applySpy).not.toHaveBeenCalled();
		expect(storeCameraOffset.offset).toEqual(ZERO_CAMERA_OFFSET);
	});
});
