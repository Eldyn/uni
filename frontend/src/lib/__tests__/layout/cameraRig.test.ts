import { describe, it, expect } from "vitest";

import { computeCameraRig } from "$components/game/layout/cameraRig";
import { SEAT_RING_RADIUS } from "$components/game/layout/seatLayout3D";
import type { ViewportInfo } from "$components/game/layout/seatLayout";

const landscape: ViewportInfo = { width: 1200, height: 800, orientation: "landscape" };
const portrait: ViewportInfo = { width: 390, height: 844, orientation: "portrait" };
const narrowPortrait: ViewportInfo = { width: 360, height: 800, orientation: "portrait" };

// Matches cameraRig.ts's own RING_COVERAGE margin — the horizontal half-width
// the camera must be able to see at its distance so no opponent seat clips
// off the sides of a narrow viewport.
const RING_COVERAGE = SEAT_RING_RADIUS + 1.5;

/** The horizontal half-width actually visible at the camera's distance from origin. */
function visibleHalfWidth(rig: ReturnType<typeof computeCameraRig>, viewport: ViewportInfo): number {
	const reach = Math.hypot(rig.position[1], rig.position[2]);
	const aspect = viewport.width / viewport.height;
	const hfov = Math.atan(Math.tan((rig.fov * Math.PI) / 360) * aspect);
	return reach * Math.tan(hfov);
}

describe("computeCameraRig", () => {
	it("always looks at the playmat center on the X/Y axes", () => {
		const rig = computeCameraRig(landscape, 3);
		expect(rig.lookAt[0]).toBe(0);
		expect(rig.lookAt[1]).toBe(0);
	});

	it("fits the full opponent ring inside the horizontal frustum in landscape", () => {
		const rig = computeCameraRig(landscape, 9);
		expect(visibleHalfWidth(rig, landscape)).toBeGreaterThanOrEqual(RING_COVERAGE - 1e-6);
	});

	it("fits the full opponent ring inside the horizontal frustum in portrait", () => {
		const rig = computeCameraRig(portrait, 9);
		expect(visibleHalfWidth(rig, portrait)).toBeGreaterThanOrEqual(RING_COVERAGE - 1e-6);
	});

	it("fits the ring on a narrower portrait phone too", () => {
		const rig = computeCameraRig(narrowPortrait, 9);
		expect(visibleHalfWidth(rig, narrowPortrait)).toBeGreaterThanOrEqual(RING_COVERAGE - 1e-6);
	});

	it("pitches portrait steeper (more top-down) than landscape", () => {
		const land = computeCameraRig(landscape, 3);
		const port = computeCameraRig(portrait, 3);
		const landPitch = Math.atan2(land.position[1], land.position[2]);
		const portPitch = Math.atan2(port.position[1], port.position[2]);
		expect(portPitch).toBeGreaterThan(landPitch);
	});

	it("never dollies inside the opponent ring", () => {
		const rig = computeCameraRig(portrait, 3);
		const reach = Math.hypot(rig.position[1], rig.position[2]);
		expect(reach).toBeGreaterThan(SEAT_RING_RADIUS);
	});

	it("clamps zoom so distance never grows or shrinks without bound", () => {
		const hugeZoomIn = computeCameraRig(landscape, 3, 1000);
		const hugeZoomOut = computeCameraRig(landscape, 3, 0.0001);
		const zoomedIn = computeCameraRig(landscape, 3, 2);
		const zoomedOut = computeCameraRig(landscape, 3, 0.5);
		expect(hugeZoomIn.position[2]).toBeCloseTo(zoomedIn.position[2], 5);
		expect(hugeZoomOut.position[2]).toBeCloseTo(zoomedOut.position[2], 5);
	});

	it("moving the camera closer (zoom > 1) shrinks distance from center", () => {
		const base = computeCameraRig(landscape, 3);
		const zoomedIn = computeCameraRig(landscape, 3, 2);
		expect(zoomedIn.position[2]).toBeLessThan(base.position[2]);
	});
});
