import { describe, expect, it } from "vitest";
import type { SeatPosition3D } from "$components/game/layout/seatLayout3D";
import {
	INHERIT_SECONDS,
	SPIN_SECONDS,
	buildSlotSequence,
	interpolateSlotPath,
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

describe("buildSlotSequence", () => {
	it("puts the bottom pose first and keeps arch order", () => {
		const bottom = seat(0, 10);
		const arch = [seat(1, 4), seat(-1, 4)];
		expect(buildSlotSequence(arch, bottom)).toEqual([bottom, ...arch]);
	});
});

describe("interpolateSlotPath", () => {
	const from = seat(0, 5, 0);
	const to = seat(5, 0, Math.PI / 2);

	it("is exact at t = 0 and t = 1", () => {
		const start = interpolateSlotPath(from, to, 0);
		expect(start.x).toBeCloseTo(from.x);
		expect(start.z).toBeCloseTo(from.z);
		expect(start.rotationY).toBeCloseTo(from.rotationY);
		const end = interpolateSlotPath(from, to, 1);
		expect(end.x).toBeCloseTo(to.x);
		expect(end.z).toBeCloseTo(to.z);
		expect(end.rotationY).toBeCloseTo(to.rotationY);
	});

	it("keeps the radius monotone and sweeps the short way", () => {
		const mid = interpolateSlotPath(from, to, 0.5);
		expect(Math.hypot(mid.x, mid.z)).toBeCloseTo(5);
		expect(Math.atan2(mid.z, mid.x)).toBeCloseTo(Math.PI / 4);
	});

	it("lerps a radial difference", () => {
		const mid = interpolateSlotPath(seat(5, 0), seat(10, 0), 0.5);
		expect(mid.x).toBeCloseTo(7.5);
	});
});

describe("spinAngleBetween", () => {
	it("signs each direction", () => {
		expect(spinAngleBetween(seat(0, 5), seat(0, 10))).toBeCloseTo(0);
		expect(spinAngleBetween(seat(5, 0), seat(0, 5))).toBeCloseTo(Math.PI / 2);
		expect(spinAngleBetween(seat(-5, 0), seat(0, 5))).toBeCloseTo(-Math.PI / 2);
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
