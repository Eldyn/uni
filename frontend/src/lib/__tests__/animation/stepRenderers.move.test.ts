import { describe, it, expect } from "vitest";
import { moveRenderer } from "$components/game/animation/stepRenderers/move";
import type { RenderContext, FlightPose } from "$components/game/animation/renderContext";

function fakeCtx(anchors: Record<string, [number, number, number]>): {
	ctx: RenderContext;
	poses: Map<string, FlightPose>;
} {
	const poses = new Map<string, FlightPose>();
	return {
		poses,
		ctx: {
			getPose(cardId, startPose) {
				if (!poses.has(cardId)) poses.set(cardId, { ...startPose });
				return poses.get(cardId)!;
			},
			resolveAnchor(name) {
				const a = anchors[name];
				if (!a) throw new Error(`no anchor for ${name}`);
				return a;
			}
		}
	};
}

describe("moveRenderer", () => {
	it("builds a timeline that tweens the card's pose to the target anchor", () => {
		const { ctx, poses } = fakeCtx({ "discard-pile": [1, 0.5, 2] });
		poses.set("card-7", {
			x: 0,
			y: 0,
			z: 0,
			spinDeg: 0,
			flipDeg: 0,
			scale: 1,
			turned: false,
			opacity: 1
		});

		const timeline = moveRenderer(
			{ op: "move", target: "card-7", payload: { to: "discard-pile" } },
			ctx
		);

		timeline.progress(1, true);
		const pose = poses.get("card-7")!;
		expect(pose.x).toBeCloseTo(1);
		expect(pose.y).toBeCloseTo(0.5);
		expect(pose.z).toBeCloseTo(2);
	});

	it("throws a descriptive error if payload.to is missing", () => {
		const { ctx } = fakeCtx({});
		expect(() => moveRenderer({ op: "move", target: "card-7", payload: {} }, ctx)).toThrow(/to/);
	});
});
