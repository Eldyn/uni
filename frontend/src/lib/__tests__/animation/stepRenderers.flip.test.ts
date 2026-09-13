import { describe, it, expect } from "vitest";
import { flipRenderer } from "$components/game/animation/stepRenderers/flip";
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
				throw new Error("flip does not resolve anchors");
			}
		}
	};
}

describe("flipRenderer", () => {
	it("ends with turned flipped from the pose's starting value", () => {
		const { ctx, poses } = fakeCtx();
		poses.set("card-1", { x: 0, y: 0, z: 0, spinDeg: 0, flipDeg: 0, scale: 1, turned: true, opacity: 1 });

		const timeline = flipRenderer({ op: "flip", target: "card-1", payload: {} }, ctx);
		timeline.progress(1, true);

		expect(poses.get("card-1")!.turned).toBe(false);
	});

	it("respects an explicit payload.turned override instead of toggling", () => {
		const { ctx, poses } = fakeCtx();
		poses.set("card-2", { x: 0, y: 0, z: 0, spinDeg: 0, flipDeg: 0, scale: 1, turned: true, opacity: 1 });

		const timeline = flipRenderer(
			{ op: "flip", target: "card-2", payload: { turned: true } },
			ctx
		);
		timeline.progress(1, true);

		expect(poses.get("card-2")!.turned).toBe(true);
	});
});
