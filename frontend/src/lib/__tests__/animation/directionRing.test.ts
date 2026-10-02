import { describe, it, expect } from "vitest";
import { directionSign, detectReverse } from "$components/game/animation/directionRing";
import { computeSeatAngles } from "$components/game/layout/seatLayout";

describe("directionSign", () => {
	it("maps positive directions to 1", () => {
		expect(directionSign(1)).toBe(1);
		expect(directionSign(3)).toBe(1);
	});

	it("maps negative and zero directions to -1", () => {
		expect(directionSign(-1)).toBe(-1);
		expect(directionSign(0)).toBe(-1);
	});
});

describe("detectReverse", () => {
	it("is true when the sign flips", () => {
		expect(detectReverse(1, -1)).toBe(true);
		expect(detectReverse(-1, 1)).toBe(true);
	});

	it("is false when the sign holds", () => {
		expect(detectReverse(1, 1)).toBe(false);
		expect(detectReverse(-1, -1)).toBe(false);
	});

	it("reports each of two reverses in a row when fed pairwise", () => {
		const directions = [1, -1, 1];
		const flips = directions.slice(1).map((next, i) => detectReverse(directions[i], next));
		expect(flips).toEqual([true, true]);
	});
});

describe("direction semantics", () => {
	// INFO: forward play (+1) steps to the next seat, which is due right of the
	//       local player, so the on-screen travel runs counter-clockwise and a
	//       positive direction sign drives the loop counter-clockwise too. This
	//       pins the convention the loop and the engine's kForward share; a sign
	//       flip here must be a deliberate, tested change.
	const viewport = { width: 1280, height: 720, orientation: "landscape" as const };

	it("places opponent 0 due right, with seat angles ascending", () => {
		const angles = computeSeatAngles(3, viewport);
		expect(angles).toHaveLength(3);
		// Ascending 0 -> 90 -> 180: opponent 0 due right, then top, then left.
		expect(angles[0]).toBeLessThan(angles[1]);
		expect(angles[1]).toBeLessThan(angles[2]);
		// Opponent 0 sits on the right half; the last seat on the left half.
		expect(Math.cos((angles[0] * Math.PI) / 180)).toBeGreaterThan(0);
		expect(Math.cos((angles[2] * Math.PI) / 180)).toBeLessThan(0);
	});
});
