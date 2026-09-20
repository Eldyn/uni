import { describe, expect, it } from "vitest";
import type { SeatPosition3D } from "$components/game/layout/seatLayout3D";
import {
	INHERIT_SECONDS,
	SPIN_SECONDS,
	rotateSlotPose,
	spinAngleBetween,
	spinDurations,
	spinStepsBetween
} from "$components/game/layout/tableSpin";

const seat = (x: number, z: number, rotationY = 0): SeatPosition3D => ({ x, z, rotationY });

describe("spinStepsBetween", () => {
	const ring = ["a", "b", "c", "d"];

	it("returns 0 for the same player", () => {
		expect(spinStepsBetween(ring, "a", "a")).toBe(0);
	});

	it("returns the forward step for a single seat", () => {
		expect(spinStepsBetween(ring, "a", "b")).toBe(1);
		expect(spinStepsBetween(ring, "b", "c")).toBe(1);
	});

	it("wraps backwards to the shortest way", () => {
		expect(spinStepsBetween(ring, "a", "d")).toBe(-1);
	});

	it("breaks an equally-short tie toward the last direction", () => {
		expect(spinStepsBetween(ring, "a", "c", 1)).toBe(2);
		expect(spinStepsBetween(ring, "a", "c", -1)).toBe(-2);
	});

	it("handles N = 2", () => {
		expect(spinStepsBetween(["a", "b"], "a", "b", 1)).toBe(1);
		expect(spinStepsBetween(["a", "b"], "a", "b", -1)).toBe(-1);
	});

	it("returns 0 for an unknown player or a single player", () => {
		expect(spinStepsBetween(ring, "a", "z")).toBe(0);
		expect(spinStepsBetween(["a"], "a", "a")).toBe(0);
	});
});

describe("rotateSlotPose", () => {
	it("rotates a position about the origin and adds the yaw to rotationY", () => {
		const rotated = rotateSlotPose(seat(5, 0, 0.2), Math.PI / 2);
		expect(rotated.x).toBeCloseTo(0);
		expect(rotated.z).toBeCloseTo(-5);
		expect(rotated.rotationY).toBeCloseTo(0.2 + Math.PI / 2);
	});

	it("is the identity at yaw 0", () => {
		const pose = seat(3, -4, 1.1);
		const rotated = rotateSlotPose(pose, 0);
		expect(rotated.x).toBeCloseTo(pose.x);
		expect(rotated.z).toBeCloseTo(pose.z);
		expect(rotated.rotationY).toBeCloseTo(pose.rotationY);
	});
});

describe("spinAngleBetween", () => {
	it("signs each direction", () => {
		expect(spinAngleBetween(seat(0, 5), seat(0, 10))).toBeCloseTo(0);
		expect(spinAngleBetween(seat(5, 0), seat(0, 5))).toBeCloseTo(-Math.PI / 2);
		expect(spinAngleBetween(seat(-5, 0), seat(0, 5))).toBeCloseTo(Math.PI / 2);
	});
});

describe("spinDurations", () => {
	it("is zero when disabled", () => {
		expect(spinDurations(1, false)).toEqual({ spin: 0, inherit: 0 });
	});

	it("scales by the speed multiplier", () => {
		expect(spinDurations(2, true)).toEqual({
			spin: SPIN_SECONDS / 2,
			inherit: INHERIT_SECONDS / 2
		});
	});

	it("clamps a non-positive multiplier", () => {
		const d = spinDurations(0, true);
		expect(d.spin).toBeGreaterThan(0);
		expect(Number.isFinite(d.spin)).toBe(true);
	});
});
