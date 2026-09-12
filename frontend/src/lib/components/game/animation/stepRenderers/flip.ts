import { gsap } from "gsap";
import type { AnimationStep } from "../types";
import type { RenderContext } from "../renderContext";

const FLIP_DURATION_S = 0.3;
// A full flip is 180deg of spin; the face swap happens at the midpoint, where
// the card is edge-on and the swap is invisible. Using timeline.set() rather
// than an onComplete callback for the face-swap write: GSAP's suppressEvents
// (the flag AnimationQueue.skipCurrent's progress(1, true) passes) suppresses
// callback functions but still applies a .set()'s property write when the
// playhead crosses it — same reason the position tweens in move.ts/shake.ts
// already survive a skip. A callback-based write would silently never run on
// skip, breaking the skip-still-completes guarantee.
const FLIP_SPIN_DEG = 180;

export function flipRenderer(step: AnimationStep, ctx: RenderContext): gsap.core.Timeline {
	const pose = ctx.getPose(step.target, {
		x: 0,
		y: 0,
		z: 0,
		spinDeg: 0,
		scale: 1,
		turned: false,
		opacity: 1
	});

	const targetTurned =
		typeof step.payload?.turned === "boolean" ? step.payload.turned : !pose.turned;
	const startSpin = pose.spinDeg;

	const timeline = gsap.timeline();
	timeline.to(pose, {
		spinDeg: startSpin + FLIP_SPIN_DEG / 2,
		duration: FLIP_DURATION_S / 2,
		ease: "power1.in"
	});
	timeline.set(pose, { turned: targetTurned });
	timeline.to(pose, {
		spinDeg: startSpin + FLIP_SPIN_DEG,
		duration: FLIP_DURATION_S / 2,
		ease: "power1.out"
	});

	return timeline;
}
