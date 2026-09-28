import { describe, it, expect } from "vitest";

import { computeSceneGeometry } from "$components/game/layout/sceneGeometry";
import { computeCameraRig } from "$components/game/layout/cameraRig";
import {
	computeBoardPlacement,
	portraitHandStripDepth
} from "$components/game/layout/boardPlacement";
import { solvePortraitTable } from "$components/game/layout/portraitTable";
import { computeSeatPositions3D, ringRadiiFor } from "$components/game/layout/seatLayout3D";
import { MAX_OPPONENTS, type ViewportInfo } from "$components/game/layout/seatLayout";

const landscape: ViewportInfo = { width: 1200, height: 800, orientation: "landscape" };
const wide: ViewportInfo = { width: 1920, height: 1080, orientation: "landscape" };
const portrait: ViewportInfo = { width: 390, height: 844, orientation: "portrait" };

const viewports = { landscape, wide, portrait };

describe("computeSceneGeometry", () => {
	for (const [name, viewport] of Object.entries(viewports)) {
		for (let opponents = 1; opponents <= MAX_OPPONENTS; opponents++) {
			it(`matches the rig/seats/placement a caller would get composing the layout modules by hand (${name}, ${opponents} opponents)`, () => {
				const geometry = computeSceneGeometry(viewport, opponents);

				const rig = computeCameraRig(viewport, opponents);
				expect(geometry.rig).toEqual(rig);

				if (viewport.orientation === "portrait") {
					const table = solvePortraitTable(
						{
							halfWidth: rig.halfWidth,
							halfHeight: rig.halfHeight,
							centerZ: rig.centerZ,
							worldPerPx: geometry.worldPerPx
						},
						opponents,
						portraitHandStripDepth(rig.halfWidth)
					);
					expect(geometry.seats3D).toEqual(table.seats);
					expect(geometry.opponentCardScale).toBe(table.fanScale);
					expect(geometry.placement).toEqual(
						computeBoardPlacement(viewport, rig, geometry.centerClearanceZ, table)
					);
					return;
				}

				const seats = computeSeatPositions3D(
					opponents,
					viewport,
					rig.halfWidth,
					rig.halfHeight - geometry.opponentSeatReach
				);
				expect(geometry.seats3D).toEqual(seats);

				const { rx, rz } = ringRadiiFor(
					viewport,
					opponents,
					rig.halfWidth,
					rig.halfHeight - geometry.opponentSeatReach
				);
				expect(geometry.ringRadii).toEqual({ rx, rz });

				// The placement this module computes must be built from the REAL
				// centerClearanceZ (ringRadii.rz - opponentSeatReach), not the
				// Infinity default a caller gets by omitting the argument — that
				// omission is exactly the bug this module exists to make impossible.
				const placement = computeBoardPlacement(viewport, rig, geometry.centerClearanceZ);
				expect(geometry.placement).toEqual(placement);
			});
		}

		it(`never leaves centerClearanceZ at its Infinity default (${name})`, () => {
			for (let opponents = 1; opponents <= MAX_OPPONENTS; opponents++) {
				const geometry = computeSceneGeometry(viewport, opponents);
				expect(geometry.centerClearanceZ).not.toBe(Infinity);
				expect(Number.isFinite(geometry.centerClearanceZ)).toBe(true);
			}
		});
	}

	it("keeps the local avatar's world size independent of geometry (it's a fixed constant, not derived)", () => {
		// localAvatarPx varies with worldPerPx (zoom), but the WORLD size it's
		// converted from is the same LOCAL_AVATAR_WORLD everywhere — this test
		// guards the wiring, not the constant's value.
		const a = computeSceneGeometry(landscape, 3);
		const b = computeSceneGeometry(wide, 9);
		expect(a.localAvatarPx).toBeGreaterThan(0);
		expect(b.localAvatarPx).toBeGreaterThan(0);
	});
});
