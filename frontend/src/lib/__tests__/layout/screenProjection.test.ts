import { describe, it, expect } from "vitest";
import { PerspectiveCamera } from "three";

import { worldToScreenPercent } from "$components/game/layout/screenProjection";
import { computeCameraRig, type CameraRig } from "$components/game/layout/cameraRig";
import { computeBoardPlacement } from "$components/game/layout/boardPlacement";
import type { ViewportInfo } from "$components/game/layout/seatLayout";

const landscape: ViewportInfo = { width: 1200, height: 800, orientation: "landscape" };
const wide: ViewportInfo = { width: 1920, height: 1080, orientation: "landscape" };
const portrait: ViewportInfo = { width: 390, height: 844, orientation: "portrait" };

const viewports = { landscape, wide, portrait };

/** Builds the same camera Scene3D mounts, from the rig the projection reads. */
function cameraFor(rig: CameraRig): PerspectiveCamera {
	const camera = new PerspectiveCamera(rig.fov, 1, rig.near, rig.far);
	camera.position.set(...rig.position);
	camera.lookAt(...rig.lookAt);
	camera.updateMatrixWorld();
	camera.updateProjectionMatrix();
	return camera;
}

describe("worldToScreenPercent", () => {
	for (const [name, viewport] of Object.entries(viewports)) {
		it(`projects the point the camera looks at to dead center (${name})`, () => {
			const rig = computeCameraRig(viewport, 9);
			const camera = cameraFor(rig);
			const { leftPercent, topPercent } = worldToScreenPercent(camera, 0, 0, rig.centerZ);
			expect(leftPercent).toBeCloseTo(50, 5);
			expect(topPercent).toBeCloseTo(50, 5);
		});

		it(`maps the board center line to the screen's horizontal center (${name})`, () => {
			const rig = computeCameraRig(viewport, 9);
			const camera = cameraFor(rig);
			const placement = computeBoardPlacement(viewport, rig);
			// A point on the board's center line (x=0) stays horizontally centered
			// regardless of its Z, because the camera is not rolled.
			const { leftPercent } = worldToScreenPercent(camera, 0, 0, placement.localSeatZ);
			expect(leftPercent).toBeCloseTo(50, 5);
		});
	}

	it("puts a +X point right of center and a -X point left of it", () => {
		const rig = computeCameraRig(landscape, 9);
		const camera = cameraFor(rig);
		const right = worldToScreenPercent(camera, rig.halfWidth / 2, 0, 0);
		const left = worldToScreenPercent(camera, -rig.halfWidth / 2, 0, 0);
		expect(right.leftPercent).toBeGreaterThan(50);
		expect(left.leftPercent).toBeLessThan(50);
		// Symmetric about center.
		expect(right.leftPercent - 50).toBeCloseTo(50 - left.leftPercent, 4);
	});

	it("foreshortens with depth — the same world offset covers less screen when farther", () => {
		const rig = computeCameraRig(landscape, 9);
		const camera = cameraFor(rig);
		// Same world X offset, two different depths. The camera is tilted toward
		// +Z, so a point at +Z is nearer and its offset covers more screen than
		// the same offset at -Z. (At the look-at depth the mapping is linear by
		// construction; the perspective divide only shows once Z differs.)
		const near = worldToScreenPercent(camera, rig.halfWidth / 2, 0, rig.centerZ + 5);
		const far = worldToScreenPercent(camera, rig.halfWidth / 2, 0, rig.centerZ - 5);
		expect(near.leftPercent - 50).toBeGreaterThan(far.leftPercent - 50);
	});
});
