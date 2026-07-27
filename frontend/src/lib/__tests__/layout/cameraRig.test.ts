import { describe, it, expect } from "vitest";

import { computeCameraRig } from "$components/game/layout/cameraRig";
import { ringReachFor } from "$components/game/layout/designGrid";
import { computeBoardPlacement } from "$components/game/layout/boardPlacement";
import { ringRadiiFor } from "$components/game/layout/seatLayout3D";
import { CARD_HEIGHT } from "$components/game/three/units";
import type { ViewportInfo } from "$components/game/layout/seatLayout";

const landscape: ViewportInfo = { width: 1200, height: 800, orientation: "landscape" };
const wide: ViewportInfo = { width: 1920, height: 1080, orientation: "landscape" };
const portrait: ViewportInfo = { width: 390, height: 844, orientation: "portrait" };
const narrowPortrait: ViewportInfo = { width: 360, height: 800, orientation: "portrait" };

// The frustum must reach past the seat ring (plus each seat's card ring +
// label reach) on both axes. ringRadiiFor's count default is the max table,
// matching boardExtentsFor.
function ringCoverageFor(viewport: ViewportInfo) {
	const { rx, rz } = ringRadiiFor(viewport);
	const reach = ringReachFor(viewport);
	return { halfWidthCoverage: rx + reach, halfHeightCoverage: rz + reach };
}

describe("computeCameraRig", () => {
	it("looks straight down (top-down, no perspective) from directly above the content center", () => {
		const rig = computeCameraRig(landscape, 3);
		expect(rig.position[0]).toBe(rig.lookAt[0]);
		expect(rig.position[2]).toBe(rig.lookAt[2]);
		expect(rig.position[1]).toBeGreaterThan(0);
	});

	for (const [name, viewport] of Object.entries({ landscape, wide, portrait, narrowPortrait })) {
		it(`keeps the world origin at the exact center of the screen (${name})`, () => {
			// The playmat and the discard pile both live at the origin, so this is
			// what makes them screen-centered on every aspect ratio.
			const rig = computeCameraRig(viewport, 9);
			expect(rig.lookAt[0]).toBeCloseTo(0, 6);
			expect(rig.lookAt[2]).toBeCloseTo(0, 6);
		});

		it(`fits the whole opponent ring in the frustum (${name})`, () => {
			const rig = computeCameraRig(viewport, 9);
			const { halfWidthCoverage, halfHeightCoverage } = ringCoverageFor(viewport);
			expect(rig.halfWidth).toBeGreaterThanOrEqual(halfWidthCoverage - 1e-6);
			expect(rig.halfHeight).toBeGreaterThanOrEqual(halfHeightCoverage - 1e-6);
		});

		it(`leaves the local hand row inside the bottom edge (${name})`, () => {
			const rig = computeCameraRig(viewport, 9);
			const placement = computeBoardPlacement(viewport, rig);
			const handFrontZ = placement.localSeatZ + (CARD_HEIGHT * placement.handScale) / 2;
			expect(handFrontZ).toBeLessThanOrEqual(rig.halfHeight + 1e-6);
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
