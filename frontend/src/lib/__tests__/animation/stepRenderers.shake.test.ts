import { describe, it, expect } from "vitest";
import { shakeRenderer } from "$components/game/animation/stepRenderers/shake";
import type { RenderContext, FlightPose } from "$components/game/animation/renderContext";

function fakeCtx(): { ctx: RenderContext; poses: Map<string, FlightPose> } {
	const poses = new Map<string, FlightPose>();
	return {
		poses,
		ctx: {
			getPose(cardId, startPose) {
				if (!poses.has(cardId)) poses.set(cardId, { ...startPose });
				return poses.get(cardId)!;
			},
			resolveAnchor() {
				throw new Error("shake does not resolve anchors");
			}
		}
	};
}

describe("shakeRenderer", () => {
	it("returns the pose's x/z to their exact starting values once complete", () => {
		const { ctx, poses } = fakeCtx();
		poses.set("card-3", { x: 5, y: 0, z: -2, spinDeg: 0, scale: 1, turned: false, opacity: 1 });

		const timeline = shakeRenderer({ op: "shake", target: "card-3", payload: {} }, ctx);
		timeline.progress(1, true);

		const pose = poses.get("card-3")!;
		expect(pose.x).toBeCloseTo(5);
		expect(pose.z).toBeCloseTo(-2);
	});
});
