import { gsap } from "gsap";
import type { AnimationStep } from "../types";
import type { RenderContext } from "../renderContext";

// An accelerating "fling" with no ease-out: the card is thrown, not eased to
// a gentle stop, so it reads as landing with real weight — the stop itself is
// covered by the shake beat baseBeats.ts queues right after this one.
const MOVE_DURATION_S = 0.25;
const MOVE_EASE = "power2.in";
// Height of the mid-flight arc lift, in world units — same idea as the old
// FlyingCardsOverlay's CSS keyframe midpoint lift, done as a real 3D arc here.
// Scaled down alongside MOVE_DURATION_S: the same lift over a much shorter
// flight reads as a steeper, more frantic pop rather than a toss.
const ARC_HEIGHT = 0.55;

export function moveRenderer(step: AnimationStep, ctx: RenderContext): gsap.core.Timeline {
	const to = step.payload?.to;
	if (typeof to !== "string") {
		throw new Error(`move step for target "${step.target}" is missing a string payload.to`);
	}

	const toScale = step.payload?.toScale;
	const toSpinDeg = step.payload?.toSpinDeg;

	const [dx, dy, dz] = ctx.resolveAnchor(to);
	const pose = ctx.getPose(step.target, {
		x: 0,
		y: 0,
		z: 0,
		spinDeg: 0,
		flipDeg: 0,
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
	// Opponent plays/draws seed at their (smaller) seat card scale and grow
	// into the discard pile's own scale mid-flight — without this the card
	// stays pinned at whichever scale it was seeded with for the whole move.
	if (typeof toScale === "number") {
		timeline.to(pose, {
			scale: toScale,
			duration: MOVE_DURATION_S,
			ease: MOVE_EASE
		}, 0);
	}
	// A landing card rotates INTO the discard pile's own random scatter angle
	// over the flight instead of teleporting onto it the instant the flight
	// hands off to the pile's static render — that instant snap is what read
	// as the card "magically" flipping to a different angle.
	if (typeof toSpinDeg === "number") {
		timeline.to(pose, {
			spinDeg: toSpinDeg,
			duration: MOVE_DURATION_S,
			ease: MOVE_EASE
		}, 0);
	}
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
