import { describe, it, expect } from "vitest";
import {
	impactProfileFor,
	IMPACT_TUNING,
	MAX_HIT_STOP_MS
} from "$components/game/animation/impactProfile";
import {
	cameraPunchOffset,
	computeCameraRig,
	offsetCameraPosition
} from "$components/game/layout/cameraRig";
import { storeCameraOffset } from "$stores/cameraOffset.svelte";

const SPECIAL_CARDS = [
	{ type: "red", value: "skip" },
	{ type: "blue", value: "reverse" },
	{ type: "green", value: "+2" },
	{ type: "white", value: "jolly" },
	{ type: "white", value: "jolly_draw4" }
];

describe("impactProfileFor", () => {
	it("returns null for a plain number card", () => {
		for (let digit = 0; digit <= 9; digit++) {
			expect(impactProfileFor({ type: "yellow", value: String(digit) })).toBeNull();
		}
	});

	it("returns a profile for skip, reverse, draw-two, wild and wild-draw-four", () => {
		for (const card of SPECIAL_CARDS) {
			expect(impactProfileFor(card)).not.toBeNull();
		}
	});

	it("never holds a hit-stop longer than 120 ms", () => {
		expect(MAX_HIT_STOP_MS).toBeLessThanOrEqual(120);
		for (const card of SPECIAL_CARDS) {
			expect(impactProfileFor(card)!.hitStopMs).toBeLessThanOrEqual(120);
		}
		for (const entry of Object.values(IMPACT_TUNING.byValue)) {
			expect(entry.hitStopMs).toBeLessThanOrEqual(120);
		}
	});

	it("punches harder for a wild draw four than for a skip", () => {
		const skip = impactProfileFor({ type: "red", value: "skip" })!;
		const drawFour = impactProfileFor({ type: "white", value: "jolly_draw4" })!;
		expect(drawFour.cameraPunch).toBeGreaterThan(skip.cameraPunch);
	});

	it("returns null for unknown or mod-defined ids without throwing", () => {
		const unknownCards = [
			{ type: "red", value: "mymod:shield" },
			{ type: "purple", value: "7" },
			{ type: "red", value: "constructor" },
			{ type: "red", value: "__proto__" },
			{ type: "", value: "" }
		];
		for (const card of unknownCards) {
			expect(() => impactProfileFor(card)).not.toThrow();
			expect(impactProfileFor(card)).toBeNull();
		}
	});
});

describe("camera offset", () => {
	const rig = computeCameraRig({ width: 1200, height: 800, orientation: "landscape" }, 3);

	it("leaves the base pose untouched with no offset", () => {
		storeCameraOffset.clearOffset();
		expect(offsetCameraPosition(rig, storeCameraOffset.offset)).toEqual(rig.position);
	});

	it("moves the camera by the applied offset", () => {
		storeCameraOffset.applyOffset({ x: 0.1, y: -0.2, z: 0.3 });
		const [x, y, z] = offsetCameraPosition(rig, storeCameraOffset.offset);
		expect(x).toBeCloseTo(rig.position[0] + 0.1);
		expect(y).toBeCloseTo(rig.position[1] - 0.2);
		expect(z).toBeCloseTo(rig.position[2] + 0.3);
		storeCameraOffset.clearOffset();
	});

	it("returns to the exact base pose after applyOffset then clearOffset", () => {
		storeCameraOffset.applyOffset(cameraPunchOffset(0.37));
		storeCameraOffset.clearOffset();
		const position = offsetCameraPosition(rig, storeCameraOffset.offset);
		expect(position[0]).toBe(rig.position[0]);
		expect(position[1]).toBe(rig.position[1]);
		expect(position[2]).toBe(rig.position[2]);
	});

	it("punches along the view axis, toward the table", () => {
		const offset = cameraPunchOffset(1);
		expect(Math.hypot(offset.x, offset.y, offset.z)).toBeCloseTo(1);
		expect(offset.y).toBeLessThan(0);
		const toLookAt = [
			rig.lookAt[0] - rig.position[0],
			rig.lookAt[1] - rig.position[1],
			rig.lookAt[2] - rig.position[2]
		];
		const length = Math.hypot(...toLookAt);
		expect(offset.x).toBeCloseTo(toLookAt[0] / length);
		expect(offset.y).toBeCloseTo(toLookAt[1] / length);
		expect(offset.z).toBeCloseTo(toLookAt[2] / length);
	});
});
