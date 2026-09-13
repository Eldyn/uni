import { gsap } from "gsap";
import type { AnimationStep } from "../types";
import type { RenderContext } from "../renderContext";

// A full flip is 90deg of tilt out to edge-on, then 90deg back down on the
// other face — the face swap happens at the edge-on midpoint, where the card
// presents no visible area and the swap is invisible. Using timeline.set()
// rather than an onComplete callback for the face-swap AND the sign-flip
// write: GSAP's suppressEvents (the flag CardRegistry.skipCurrent's
// progress(1, true) passes) suppresses callback functions but still applies a
// .set()'s property write when the playhead crosses it — same reason the
// position tweens in move.ts/shake.ts already survive a skip. A
// callback-based write would silently never run on skip, breaking the
// skip-still-completes guarantee.
export const FLIP_DURATION_S = 0.225;
const FLIP_EDGE_DEG = 90;

export function flipRenderer(step: AnimationStep, ctx: RenderContext): gsap.core.Timeline {
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
	const targetTurned =
		typeof step.payload?.turned === "boolean" ? step.payload.turned : !pose.turned;
	if (step.payload?.axis === "x" || step.payload?.axis === "y") {
		pose.flipAxis = step.payload.axis;
	} else if (!pose.flipAxis) {
		pose.flipAxis = "x";
	}

	const timeline = gsap.timeline();
	timeline.to(pose, {
		flipDeg: FLIP_EDGE_DEG,
		duration: FLIP_DURATION_S / 2,
		ease: "power1.in"
	});
	// The sign flip at the midpoint (edge-on, so invisible) keeps the revealed
	// face unmirrored — a straight 0->180 sweep on one signed axis would leave
	// text/numbers backwards on the far side. spinDeg is not touched at all —
	// flip and spin are fully independent axes now (see CardMesh3D.svelte's
	// Rx * Ry(flip) * Rz(spin) composition).
	timeline.set(pose, { turned: targetTurned, flipDeg: -FLIP_EDGE_DEG });
	timeline.to(pose, {
		flipDeg: 0,
		duration: FLIP_DURATION_S / 2,
		ease: "power1.out"
	});

	return timeline;
}
