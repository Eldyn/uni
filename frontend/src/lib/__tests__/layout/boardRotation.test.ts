import { describe, expect, it } from "vitest";
import {
	boardRotationFor,
	rotateBoardPoint,
	rotateBoardPose,
	rotateBoardSpinDeg
} from "$components/game/layout/boardRotation";

describe("rotateBoardPoint", () => {
	it("is the identity at yaw 0", () => {
		expect(rotateBoardPoint({ x: 3, z: -4 }, 0)).toEqual({ x: 3, z: -4 });
	});

	it("uses the same convention as a parent group's rotation.y", () => {
		// A point on +X rotates to -Z (counter-clockwise from above), matching
		// three.js: worldX = x*cos + z*sin, worldZ = -x*sin + z*cos.
		const rotated = rotateBoardPoint({ x: 5, z: 0 }, Math.PI / 2);
		expect(rotated.x).toBeCloseTo(0, 6);
		expect(rotated.z).toBeCloseTo(-5, 6);
	});

	it("preserves the radius from the board center", () => {
		const rotated = rotateBoardPoint({ x: 3, z: 4 }, 1.234);
		expect(Math.hypot(rotated.x, rotated.z)).toBeCloseTo(5, 6);
	});
});

describe("rotateBoardPose", () => {
	it("leaves Y untouched and rotates X/Z", () => {
		const rotated = rotateBoardPose([5, 0.6, 0], Math.PI / 2);
		expect(rotated[0]).toBeCloseTo(0, 6);
		expect(rotated[1]).toBe(0.6);
		expect(rotated[2]).toBeCloseTo(-5, 6);
	});
});

describe("rotateBoardSpinDeg", () => {
	it("folds the yaw into a card's own in-plane spin", () => {
		expect(rotateBoardSpinDeg(15, Math.PI / 2)).toBeCloseTo(15 + 90, 6);
	});
});

describe("boardRotationFor", () => {
	const pivot = { x: 0, z: 10 };

	it("is zero when the seat already sits on the pivot's bearing", () => {
		expect(boardRotationFor({ x: 0, z: 5 }, pivot)).toBeCloseTo(0, 6);
	});

	it("turns a side seat onto the bottom pivot", () => {
		// A seat on the +X axis swings to the +Z pivot. Under this module's
		// convention the required yaw is negative (see rotateBoardPoint).
		expect(boardRotationFor({ x: 5, z: 0 }, pivot)).toBeCloseTo(-Math.PI / 2, 6);
	});

	it("signs the other side the opposite way", () => {
		expect(boardRotationFor({ x: -5, z: 0 }, pivot)).toBeCloseTo(Math.PI / 2, 6);
	});

	it("always takes the shortest path", () => {
		expect(boardRotationFor({ x: 0, z: -5 }, pivot)).toBeCloseTo(Math.PI, 6);
	});

	it("actually lands the seat on the pivot when applied", () => {
		const seat = { x: 3, z: -4 };
		const yaw = boardRotationFor(seat, pivot);
		const landed = rotateBoardPoint(seat, yaw);
		// Same bearing as the pivot, so the seat is now on the pivot's line.
		expect(Math.atan2(landed.z, landed.x)).toBeCloseTo(Math.atan2(pivot.z, pivot.x), 6);
	});
});
