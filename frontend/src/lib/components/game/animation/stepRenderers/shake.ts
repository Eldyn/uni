import { gsap } from "gsap";
import type { AnimationStep } from "../types";
import type { RenderContext } from "../renderContext";

const SHAKE_DURATION_S = 0.135;
const SHAKE_MAGNITUDE = 0.06;

export function shakeRenderer(step: AnimationStep, ctx: RenderContext): gsap.core.Timeline {
	const pose = ctx.getPose(step.target, {
		x: 0,
		y: 0,
		z: 0,
		spinDeg: 0,
		scale: 1,
		turned: false,
		opacity: 1
	});
	const originX = pose.x;
	const originZ = pose.z;

	// A punchy landing shake: two quick over/undershoots on X, settling back
	// to the exact origin — gsap.utils's keyframes array does this in one tween
	// instead of chaining several .to() calls.
	return gsap.timeline().to(pose, {
		keyframes: {
			x: [originX + SHAKE_MAGNITUDE, originX - SHAKE_MAGNITUDE * 0.6, originX],
			z: [originZ, originZ, originZ]
		},
		duration: SHAKE_DURATION_S,
		ease: "power1.out"
	});
}
