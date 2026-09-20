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
	it("sits above the content center, tilted off vertical toward the player", () => {
		const rig = computeCameraRig(landscape, 3);
		// Horizontal centering is unchanged: the camera is on the board's own
		// center line, looking down at it.
		expect(rig.position[0]).toBe(rig.lookAt[0]);
		// The tilt is the whole point of the perspective camera — the camera is
		// NOT directly above the look-at point any more, it sits toward the local
		// (+Z) side so the far edge of the mat is seen at a shallow angle.
		expect(rig.position[2]).toBeGreaterThan(rig.lookAt[2]);
		expect(rig.position[1]).toBeGreaterThan(0);
		expect(rig.tiltRad).toBeGreaterThan(0);
	});

	it("places the camera at the lid of a cone whose tilt matches the rig", () => {
		const rig = computeCameraRig(wide, 9);
		const dx = rig.position[0] - rig.lookAt[0];
		const dy = rig.position[1] - rig.lookAt[1];
		const dz = rig.position[2] - rig.lookAt[2];
		// Distance is exactly the hypotenuse, and the tilt is its angle from
		// vertical (the Y axis).
		expect(Math.hypot(dx, dy, dz)).toBeCloseTo(rig.distance, 5);
		expect(Math.atan2(Math.abs(dz), dy)).toBeCloseTo(rig.tiltRad, 5);
	});

	it("sizes the perspective distance so the board's NEAR edge fits the frustum", () => {
		const rig = computeCameraRig(landscape, 9);
		const halfFovRad = (rig.fov * Math.PI) / 360;
		const cosT = Math.cos(rig.tiltRad);
		const sinT = Math.sin(rig.tiltRad);
		const tanHalfFov = Math.tan(halfFovRad);
		// The defining relation: the near edge's axis distance times tan(fov/2)
		// equals the board's half-height, i.e. the near edge touches the bottom of
		// the frustum exactly. (At the look-at plane the coverage is smaller than
		// halfHeight because that plane is farther from the tilted camera.)
		const nearAxisDistance = rig.distance - sinT * rig.halfHeight;
		expect(nearAxisDistance * tanHalfFov).toBeCloseTo(cosT * rig.halfHeight, 4);
	});

	for (const [name, viewport] of Object.entries({ landscape, wide, portrait, narrowPortrait })) {
		it(`keeps the board horizontally centered and only ever biases toward the player (${name})`, () => {
			// The table stays on the screen's vertical center line on every aspect
			// ratio. On the near/far axis a portrait screen has vertical slack the
			// board's own rows don't need, and the camera spends it toward the local
			// player rather than leaving half of it as dead mat under the HUD (see
			// designGrid.ts's PORTRAIT_NEAR_BIAS) — never away from them.
			const rig = computeCameraRig(viewport, 9);
			expect(rig.lookAt[0]).toBeCloseTo(0, 6);
			expect(rig.lookAt[2]).toBe(rig.centerZ);
			expect(rig.centerZ).toBeGreaterThanOrEqual(0);
			if (viewport.orientation === "landscape") expect(rig.centerZ).toBeCloseTo(0, 6);
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
			expect(handFrontZ).toBeLessThanOrEqual(rig.centerZ + rig.halfHeight + 1e-6);
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
