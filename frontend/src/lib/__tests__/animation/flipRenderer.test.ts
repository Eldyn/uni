import { describe, it, expect } from "vitest";
import { flipRenderer, FLIP_DURATION_S } from "$components/game/animation/stepRenderers/flip";
import type { RenderContext, FlightPose } from "$components/game/animation/renderContext";

function makeCtx(pose: FlightPose): RenderContext {
	return {
		getPose: () => pose,
		resolveAnchor: () => [0, 0, 0]
	};
}

describe("flipRenderer", () => {
	it("tweens flipDeg, never touches spinDeg, and survives a skip via timeline.set for `turned`", () => {
		const pose: FlightPose = {
			x: 0,
			y: 0,
			z: 0,
			spinDeg: 42,
			flipDeg: 0,
			scale: 1,
			turned: false,
			opacity: 1
		};
		const ctx = makeCtx(pose);
		const timeline = flipRenderer({ op: "flip", target: "1", payload: { turned: true } }, ctx);

		timeline.progress(1, true); // GSAP suppressEvents, same as skipCurrent

		expect(pose.spinDeg).toBe(42); // untouched
		expect(pose.turned).toBe(true); // survives skip via .set(), not onComplete
		expect(pose.flipDeg).toBeCloseTo(0); // ends back at 0 (0 -> 90 -> -90 -> 0 net)
	});
});
