import { describe, expect, it } from "vitest";
import {
	anchorWithBoardRotation,
	poseWithBoardRotation
} from "$components/game/animation/cardBoardPose";

describe("poseWithBoardRotation", () => {
	it("leaves a hand card (not table-bound) untouched at any yaw", () => {
		const pose = { x: 4, y: 0.2, z: 1, spinDeg: 30, tableBound: false };
		const out = poseWithBoardRotation(pose, Math.PI / 2);
		expect(out).toEqual({ x: 4, y: 0.2, z: 1, spinDeg: 30 });
	});

	it("rotates a table-bound card's position and spin", () => {
		const pose = { x: 5, y: 0.4, z: 0, spinDeg: 0, tableBound: true };
		const out = poseWithBoardRotation(pose, Math.PI / 2);
		expect(out.x).toBeCloseTo(0, 6);
		expect(out.y).toBe(0.4);
		expect(out.z).toBeCloseTo(-5, 6);
		expect(out.spinDeg).toBeCloseTo(90, 6);
	});

	it("is a no-op at yaw 0 even for table-bound cards", () => {
		const pose = { x: 5, y: 0, z: 2, spinDeg: 12, tableBound: true };
		expect(poseWithBoardRotation(pose, 0)).toEqual({ x: 5, y: 0, z: 2, spinDeg: 12 });
	});

	it("does not mutate the source pose (it is the registry's reactive state)", () => {
		const pose = { x: 5, y: 0, z: 0, spinDeg: 0, tableBound: true };
		poseWithBoardRotation(pose, Math.PI / 2);
		expect(pose.x).toBe(5);
		expect(pose.z).toBe(0);
	});
});

describe("anchorWithBoardRotation", () => {
	it("rotates a table anchor about the origin", () => {
		const out = anchorWithBoardRotation([5, 0.6, 0], Math.PI / 2);
		expect(out[0]).toBeCloseTo(0, 6);
		expect(out[1]).toBe(0.6);
		expect(out[2]).toBeCloseTo(-5, 6);
	});

	it("is a no-op at yaw 0", () => {
		expect(anchorWithBoardRotation([1, 2, 3], 0)).toEqual([1, 2, 3]);
	});
});
