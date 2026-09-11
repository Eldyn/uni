import { gsap } from "gsap";
import type { AnimationStep } from "../types";
import type { RenderContext } from "../renderContext";

// A punchy, cinematic ease-in-out rather than linear, so base movements read as
// "good in-out frames".
const MOVE_DURATION_S = 0.45;
const MOVE_EASE = "power2.inOut";
// Height of the mid-flight arc lift, in world units — same idea as the old
// FlyingCardsOverlay's CSS keyframe midpoint lift, done as a real 3D arc here.
const ARC_HEIGHT = 0.8;

export function moveRenderer(step: AnimationStep, ctx: RenderContext): gsap.core.Timeline {
	const to = step.payload?.to;
	if (typeof to !== "string") {
		throw new Error(`move step for target "${step.target}" is missing a string payload.to`);
	}

	const [dx, dy, dz] = ctx.resolveAnchor(to);
	const pose = ctx.getPose(step.target, {
		x: 0,
		y: 0,
		z: 0,
		spinDeg: 0,
		scale: 1,
		turned: false,
		opacity: 1
	});

	const timeline = gsap.timeline();
	timeline.to(pose, {
		x: dx,
		z: dz,
		duration: MOVE_DURATION_S,
		ease: MOVE_EASE
	}, 0);
	// The arc: up then back down, independent of the X/Z tween above so it
	// reads as a toss rather than a straight glide.
	timeline.to(pose, {
		y: dy + ARC_HEIGHT,
		duration: MOVE_DURATION_S / 2,
		ease: "power1.out"
	}, 0);
	timeline.to(pose, {
		y: dy,
		duration: MOVE_DURATION_S / 2,
		ease: "power1.in"
	}, MOVE_DURATION_S / 2);

	return timeline;
}
