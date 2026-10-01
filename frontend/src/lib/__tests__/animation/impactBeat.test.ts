import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { gsap } from "gsap";
import { CardRegistry } from "$components/game/animation/cardRegistry.svelte";
import { buildLandingImpactBeat } from "$components/game/animation/baseBeats.svelte";
import {
	IMPACT_TUNING,
	impactProfileFor,
	type ImpactProfile
} from "$components/game/animation/impactProfile";
import { SHAKE_DURATION_S } from "$components/game/animation/stepRenderers/shake";
import { ZERO_CAMERA_OFFSET } from "$components/game/layout/cameraRig";
import { storeAnimation } from "$stores/animation.svelte";
import { storeCameraOffset } from "$stores/cameraOffset.svelte";

const USER_SPEED = 1.5;
const DRAW_FOUR = impactProfileFor({ type: "white", value: "jolly_draw4" })!;

/** Drives GSAP's root clock by hand so the beat timeline is deterministic. */
function advance(seconds: number): void {
	gsap.updateRoot(gsap.globalTimeline.time() + seconds);
}

function resolveAnchor(): [number, number, number] {
	return [0, 0, 0];
}

/** Enqueues the landing impact beat and returns the beat timeline the
 *  registry built for it (the first gsap.timeline() call of the beat). */
function playImpact(registry: CardRegistry, profile: ImpactProfile | null) {
	const timelineSpy = vi.spyOn(gsap, "timeline");
	registry.registerCardMeta("card-1", { type: "white", value: "jolly_draw4" });
	registry.seedPose("card-1", { x: 0, y: 0, z: 0 });
	const done = registry.enqueue([buildLandingImpactBeat("card-1", profile)], resolveAnchor);
	const beatTimeline = timelineSpy.mock.results[0]!.value as gsap.core.Timeline;
	timelineSpy.mockRestore();
	return { done, beatTimeline };
}

describe("landing impact beat", () => {
	beforeEach(() => {
		gsap.ticker.lagSmoothing(false);
		storeAnimation.enabled = true;
		storeAnimation.speedMultiplier = USER_SPEED;
		storeCameraOffset.clearOffset();
	});

	afterEach(() => {
		storeAnimation.speedMultiplier = 1;
		storeCameraOffset.clearOffset();
		vi.restoreAllMocks();
	});

	it("is the plain landing shake when the card has no impact", () => {
		expect(buildLandingImpactBeat("card-1", null).map((step) => step.op)).toEqual(["shake"]);
	});

	it("adds one screen-level impact step beside the shake for a special card", () => {
		const beat = buildLandingImpactBeat("card-1", DRAW_FOUR);
		expect(beat.map((step) => step.op)).toEqual(["shake", "impact"]);
		expect(beat[1]!.target).toBe("screen");
		expect(beat[1]!.payload?.profile).toEqual(DRAW_FOUR);
	});

	it("never lengthens the landing beat beyond the shake", () => {
		const punchS = IMPACT_TUNING.punchAttackS + IMPACT_TUNING.punchReleaseS;
		expect(punchS).toBeLessThanOrEqual(SHAKE_DURATION_S);
		const registry = new CardRegistry();
		const { beatTimeline } = playImpact(registry, DRAW_FOUR);
		expect(beatTimeline.duration()).toBeCloseTo(SHAKE_DURATION_S);
		registry.flushImmediately();
	});

	it("holds the beat in a hit-stop, then restores the user's speed", async () => {
		const registry = new CardRegistry();
		const { done, beatTimeline } = playImpact(registry, DRAW_FOUR);
		expect(beatTimeline.timeScale()).toBe(USER_SPEED);

		advance(0.001);
		expect(beatTimeline.timeScale()).toBe(IMPACT_TUNING.hitStopTimeScale);

		advance(DRAW_FOUR.hitStopMs / USER_SPEED / 1000 + 0.01);
		expect(beatTimeline.timeScale()).toBe(USER_SPEED);

		advance(1);
		await done;
		expect(storeCameraOffset.offset).toEqual(ZERO_CAMERA_OFFSET);
	});

	it("punches the camera", () => {
		const applySpy = vi.spyOn(storeCameraOffset, "applyOffset");
		const registry = new CardRegistry();
		playImpact(registry, DRAW_FOUR);

		advance(0.001);
		advance(DRAW_FOUR.hitStopMs / USER_SPEED / 1000 + 0.02);

		expect(applySpy).toHaveBeenCalled();
		const punched = applySpy.mock.calls.some(([offset]) => offset.y < 0);
		expect(punched).toBe(true);
		registry.flushImmediately();
	});

	it("restores speed and camera when skipped mid hit-stop", async () => {
		const registry = new CardRegistry();
		const { done, beatTimeline } = playImpact(registry, DRAW_FOUR);
		advance(0.001);
		advance(0.01);
		expect(beatTimeline.timeScale()).toBe(IMPACT_TUNING.hitStopTimeScale);

		registry.skipCurrent();
		await done;

		expect(beatTimeline.timeScale()).toBe(USER_SPEED);
		expect(storeCameraOffset.offset).toEqual(ZERO_CAMERA_OFFSET);

		// The pending restore was cancelled with the beat, not left to fire.
		advance(1);
		expect(beatTimeline.timeScale()).toBe(USER_SPEED);
	});

	it("skips hit-stop and punch entirely while fast-forwarding", async () => {
		const applySpy = vi.spyOn(storeCameraOffset, "applyOffset");
		const createTimeline = gsap.timeline.bind(gsap);
		const timeScaleSpies: Array<ReturnType<typeof vi.spyOn>> = [];
		vi.spyOn(gsap, "timeline").mockImplementation((vars) => {
			const timeline = createTimeline(vars);
			timeScaleSpies.push(vi.spyOn(timeline, "timeScale"));
			return timeline;
		});
		const registry = new CardRegistry();
		registry.registerCardMeta("card-1", { type: "white", value: "jolly_draw4" });
		registry.seedPose("card-0", { x: 0, y: 0, z: 0 });
		registry.seedPose("card-1", { x: 0, y: 0, z: 0 });
		registry.enqueue(
			[[{ op: "move", target: "card-0", payload: { to: "discard-pile" } }]],
			resolveAnchor
		);
		const done = registry.enqueue([buildLandingImpactBeat("card-1", DRAW_FOUR)], resolveAnchor);

		registry.flushImmediately();
		await done;
		advance(1);

		expect(timeScaleSpies.length).toBeGreaterThan(1);
		for (const spy of timeScaleSpies) {
			expect(spy).not.toHaveBeenCalledWith(IMPACT_TUNING.hitStopTimeScale);
		}
		expect(applySpy).not.toHaveBeenCalled();
		expect(storeCameraOffset.offset).toEqual(ZERO_CAMERA_OFFSET);
	});

	it("skips the impact of a beat that starts because the user skipped the flight", () => {
		const applySpy = vi.spyOn(storeCameraOffset, "applyOffset");
		const registry = new CardRegistry();
		registry.registerCardMeta("card-1", { type: "white", value: "jolly_draw4" });
		registry.seedPose("card-1", { x: 0, y: 0, z: 0 });
		const timelineSpy = vi.spyOn(gsap, "timeline");
		registry.enqueue(
			[
				[{ op: "move", target: "card-1", payload: { to: "discard-pile" } }],
				buildLandingImpactBeat("card-1", DRAW_FOUR)
			],
			resolveAnchor
		);
		const flightTimeline = timelineSpy.mock.results[0]!.value as gsap.core.Timeline;

		registry.skipCurrent();
		const impactTimeline = timelineSpy.mock.results
			.map((result) => result.value as gsap.core.Timeline)
			.find((timeline) => timeline !== flightTimeline && timeline.timeScale() === USER_SPEED)!;
		timelineSpy.mockRestore();

		advance(0.001);
		advance(0.05);
		expect(impactTimeline.timeScale()).toBe(USER_SPEED);
		expect(applySpy).not.toHaveBeenCalled();
		registry.flushImmediately();
	});
});
