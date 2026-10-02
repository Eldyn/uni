import { describe, it, expect, vi, afterEach } from "vitest";
import { gsap } from "gsap";
import { TurnLoopMotion } from "$components/game/three/turnLoopMotion";
import {
	LOOP_IDLE_TONE,
	LOOP_MARCH_BLOCKS_PER_SECOND,
	LOOP_PEAK_TONE
} from "$components/game/animation/loopPlan";

function build(motionActive = true) {
	const state = { motionActive, speed: 1 };
	const onChange = vi.fn();
	const motion = new TurnLoopMotion({
		motionActive: () => state.motionActive,
		speedMultiplier: () => state.speed,
		onChange
	});
	motion.loopLength = 100;
	return { motion, state, onChange };
}

function tweenAt(spy: ReturnType<typeof vi.spyOn>, index: number): gsap.core.Tween {
	return spy.mock.results[index].value as gsap.core.Tween;
}

describe("TurnLoopMotion", () => {
	afterEach(() => {
		vi.restoreAllMocks();
		gsap.globalTimeline.clear();
	});

	describe("march", () => {
		it("advances the phase along the current direction each tick", () => {
			const { motion, onChange } = build();
			motion.tick(1);
			expect(motion.phase.value).toBeCloseTo(LOOP_MARCH_BLOCKS_PER_SECOND);
			expect(onChange).toHaveBeenCalled();
		});

		it("scales the march with the animation speed", () => {
			const { motion, state } = build();
			state.speed = 2;
			motion.tick(1);
			expect(motion.phase.value).toBeCloseTo(2 * LOOP_MARCH_BLOCKS_PER_SECOND);
		});

		it("does not move when motion is off", () => {
			const { motion, state, onChange } = build();
			state.motionActive = false;
			motion.tick(1);
			expect(motion.phase.value).toBe(0);
			expect(onChange).not.toHaveBeenCalled();
		});
	});

	describe("reverse", () => {
		it("flips the direction at once and plays one brighten impulse", () => {
			const toSpy = vi.spyOn(gsap, "to");
			const { motion } = build();
			motion.enqueue([-1]);
			expect(motion.direction.value).toBe(-1);
			expect(toSpy).toHaveBeenCalledTimes(1);

			tweenAt(toSpy, 0).progress(0.5);
			expect(motion.tone.value).toBeCloseTo(LOOP_PEAK_TONE);
			tweenAt(toSpy, 0).progress(1);
			expect(motion.tone.value).toBeCloseTo(LOOP_IDLE_TONE);
		});

		it("does not jump the phase when the direction flips", () => {
			const { motion } = build();
			motion.tick(1);
			const before = motion.phase.value;
			motion.enqueue([-1]);
			expect(motion.phase.value).toBe(before);
			motion.tick(1);
			expect(motion.phase.value).toBeCloseTo(before - LOOP_MARCH_BLOCKS_PER_SECOND);
		});

		it("runs two reverses from one batch in order, one impulse each", () => {
			const toSpy = vi.spyOn(gsap, "to");
			const { motion } = build();
			motion.enqueue([-1, 1]);
			expect(motion.direction.value).toBe(-1);
			expect(toSpy).toHaveBeenCalledTimes(1);

			tweenAt(toSpy, 0).progress(1);
			expect(motion.direction.value).toBe(1);
			expect(toSpy).toHaveBeenCalledTimes(2);

			tweenAt(toSpy, 1).progress(1);
			expect(motion.tone.value).toBeCloseTo(LOOP_IDLE_TONE);
		});

		it("applies the last flip instantly with no tween when motion is off", () => {
			const toSpy = vi.spyOn(gsap, "to");
			const { motion, state } = build(false);
			state.motionActive = false;
			motion.enqueue([-1, 1, -1]);
			expect(motion.direction.value).toBe(-1);
			expect(motion.tone.value).toBe(LOOP_IDLE_TONE);
			expect(motion.phase.value).toBe(0);
			expect(toSpy).not.toHaveBeenCalled();
		});
	});

	describe("settling", () => {
		it("snaps to the server direction and drops queued flips", () => {
			const toSpy = vi.spyOn(gsap, "to");
			const { motion } = build();
			motion.enqueue([-1, 1, -1]);
			motion.snapTo(1);
			expect(motion.direction.value).toBe(1);
			expect(motion.tone.value).toBe(LOOP_IDLE_TONE);
			expect(gsap.getTweensOf(tweenAt(toSpy, 0).targets())).toHaveLength(0);
			expect(toSpy).toHaveBeenCalledTimes(1);
		});

		it("keeps the phase when settling while motion is on", () => {
			const { motion } = build();
			motion.tick(1);
			const before = motion.phase.value;
			motion.snapTo(-1);
			expect(motion.phase.value).toBe(before);
		});

		// Review focus 2: switching motion off mid-flip must not leave the loop bright.
		it("snaps to the last queued direction and clears the glow when motion stops mid-flip", () => {
			const toSpy = vi.spyOn(gsap, "to");
			const { motion, state } = build();
			motion.tick(1);
			motion.enqueue([-1, 1]);
			tweenAt(toSpy, 0).progress(0.5);
			expect(motion.tone.value).toBeGreaterThan(LOOP_IDLE_TONE);

			state.motionActive = false;
			motion.motionStopped();

			expect(motion.direction.value).toBe(1);
			expect(motion.tone.value).toBe(LOOP_IDLE_TONE);
			expect(motion.phase.value).toBe(0);
			expect(gsap.getTweensOf(tweenAt(toSpy, 0).targets())).toHaveLength(0);
		});

		it("kills any running tween on dispose", () => {
			const toSpy = vi.spyOn(gsap, "to");
			const { motion } = build();
			motion.enqueue([-1]);
			motion.dispose();
			expect(gsap.getTweensOf(tweenAt(toSpy, 0).targets())).toHaveLength(0);
		});
	});
});
