import { describe, it, expect } from "vitest";
import { CARD_HOVER_SCALE, CARD_HOVER_LIFT } from "$components/game/three/units";

describe("CardMesh3D constants & pose derivation", () => {
	it("has CARD_HOVER_SCALE set to 1.2 matching Balatro feel", () => {
		expect(CARD_HOVER_SCALE).toBe(1.2);
	});

	it("computes animated scale correctly from liftT and CARD_HOVER_SCALE", () => {
		const scale = 1;
		const computeScale = (liftT: number) => scale * (1 + (CARD_HOVER_SCALE - 1) * liftT);

		expect(computeScale(0)).toBe(1);
		expect(computeScale(1)).toBe(1.2);
		expect(computeScale(0.5)).toBeCloseTo(1.1);
	});

	it("computes animated position correctly from liftT, pushX, and hoverPush", () => {
		const position: [number, number, number] = [1, 2, 3];
		const hoverPush: [number, number] = [0.1, -0.2];
		const flipLift = 0;

		const computePosition = (liftT: number, pushX: number): [number, number, number] => [
			position[0] + hoverPush[0] * liftT + pushX,
			position[1] + CARD_HOVER_LIFT * liftT + flipLift,
			position[2] + hoverPush[1] * liftT
		];

		// At rest
		expect(computePosition(0, 0)).toEqual([1, 2, 3]);

		// With neighbor push
		expect(computePosition(0, 0.4)).toEqual([1.4, 2, 3]);

		// Fully lifted with push
		const lifted = computePosition(1, 0.2);
		expect(lifted[0]).toBeCloseTo(1.3);
		expect(lifted[1]).toBeCloseTo(2 + CARD_HOVER_LIFT);
		expect(lifted[2]).toBeCloseTo(2.8);
	});

	it("computes totalSpinDeg factoring in hoverSpinDeg", () => {
		const spinDeg = 10;
		const hoverSpinDeg = 5;
		const computeSpin = (spin: number, hoverSpin: number) => spin + hoverSpin;

		expect(computeSpin(spinDeg, 0)).toBe(10);
		expect(computeSpin(spinDeg, hoverSpinDeg)).toBe(15);
	});
});
