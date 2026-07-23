import { describe, it, expect } from "vitest";

import { computeCameraRig } from "$components/game/layout/cameraRig";
import { SEAT_RING_RADIUS } from "$components/game/layout/seatLayout3D";
import { LOCAL_SEAT_Z } from "$components/game/three/units";
import type { ViewportInfo } from "$components/game/layout/seatLayout";

const landscape: ViewportInfo = { width: 1200, height: 800, orientation: "landscape" };
const portrait: ViewportInfo = { width: 390, height: 844, orientation: "portrait" };
const narrowPortrait: ViewportInfo = { width: 360, height: 800, orientation: "portrait" };

const HALF_WIDTH_COVERAGE = SEAT_RING_RADIUS + 1.5;
const FAR_EDGE_Z = -SEAT_RING_RADIUS;
const NEAR_EDGE_Z = LOCAL_SEAT_Z + 2.5;

describe("computeCameraRig", () => {
	it("looks straight down (top-down, no perspective) from directly above the content center", () => {
		const rig = computeCameraRig(landscape, 3);
		expect(rig.position[0]).toBe(rig.lookAt[0]);
		expect(rig.position[2]).toBe(rig.lookAt[2]);
		expect(rig.position[1]).toBeGreaterThan(0);
	});

	it("centers the look-at point between the far opponent ring and the local seat", () => {
		const rig = computeCameraRig(landscape, 3);
		expect(rig.lookAt[2]).toBeCloseTo((FAR_EDGE_Z + NEAR_EDGE_Z) / 2, 5);
	});

	for (const [name, viewport] of Object.entries({ landscape, portrait, narrowPortrait })) {
		it(`fits both the opponent ring and the local seat in the frustum (${name})`, () => {
			const rig = computeCameraRig(viewport, 9);
			expect(rig.halfWidth).toBeGreaterThanOrEqual(HALF_WIDTH_COVERAGE - 1e-6);
			const topZ = rig.lookAt[2] - rig.halfHeight;
			const bottomZ = rig.lookAt[2] + rig.halfHeight;
			expect(topZ).toBeLessThanOrEqual(FAR_EDGE_Z + 1e-6);
			expect(bottomZ).toBeGreaterThanOrEqual(NEAR_EDGE_Z - 1e-6);
		});
	}

	it("keeps the frustum's aspect ratio matching the viewport", () => {
		const rig = computeCameraRig(portrait, 3);
		expect(rig.halfWidth / rig.halfHeight).toBeCloseTo(portrait.width / portrait.height, 5);
	});

	it("clamps zoom so the frustum never grows or shrinks without bound", () => {
		const hugeZoomIn = computeCameraRig(landscape, 3, 1000);
		const hugeZoomOut = computeCameraRig(landscape, 3, 0.0001);
		const zoomedIn = computeCameraRig(landscape, 3, 2);
		const zoomedOut = computeCameraRig(landscape, 3, 0.5);
		expect(hugeZoomIn.halfHeight).toBeCloseTo(zoomedIn.halfHeight, 5);
		expect(hugeZoomOut.halfHeight).toBeCloseTo(zoomedOut.halfHeight, 5);
	});

	it("zooming in (zoom > 1) shrinks the visible frustum", () => {
		const base = computeCameraRig(landscape, 3);
		const zoomedIn = computeCameraRig(landscape, 3, 2);
		expect(zoomedIn.halfHeight).toBeLessThan(base.halfHeight);
	});
});
