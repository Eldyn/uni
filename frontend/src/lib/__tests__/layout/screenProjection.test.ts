import { describe, it, expect } from "vitest";

import { worldToScreenPercent } from "$components/game/layout/screenProjection";
import { computeCameraRig } from "$components/game/layout/cameraRig";
import { computeBoardPlacement } from "$components/game/layout/boardPlacement";
import type { ViewportInfo } from "$components/game/layout/seatLayout";

const landscape: ViewportInfo = { width: 1200, height: 800, orientation: "landscape" };
const wide: ViewportInfo = { width: 1920, height: 1080, orientation: "landscape" };
const portrait: ViewportInfo = { width: 390, height: 844, orientation: "portrait" };

const viewports = { landscape, wide, portrait };

// The formula GameBoard.svelte used before this module existed: a single
// pixels-per-world-unit constant (derived from the viewport's HEIGHT, which
// only equals the width-derived constant because the frustum always matches
// the viewport's exact aspect ratio — see designGrid.ts), added as raw
// pixels to a 50% CSS anchor.
function legacyPercent(
	viewport: ViewportInfo,
	halfHeight: number,
	worldOffset: number,
	axisPx: number
) {
	const pxPerUnit = viewport.height / (2 * halfHeight);
	return 50 + (worldOffset * pxPerUnit * 100) / axisPx;
}

describe("worldToScreenPercent", () => {
	for (const [name, viewport] of Object.entries(viewports)) {
		it(`projects the point the camera looks at to dead center (${name})`, () => {
			const rig = computeCameraRig(viewport, 9);
			const { leftPercent, topPercent } = worldToScreenPercent(rig, 0, rig.centerZ);
			expect(leftPercent).toBeCloseTo(50, 5);
			expect(topPercent).toBeCloseTo(50, 5);
		});

		it(`matches the legacy hand-rolled pixel formula for a nonzero pile offset (${name})`, () => {
			const rig = computeCameraRig(viewport, 9);
			const placement = computeBoardPlacement(viewport, rig);

			const { leftPercent, topPercent } = worldToScreenPercent(
				rig,
				placement.drawPileX,
				placement.localSeatZ
			);

			expect(leftPercent).toBeCloseTo(
				legacyPercent(viewport, rig.halfHeight, placement.drawPileX, viewport.width),
				5
			);
			// The legacy formula measured every offset from the world origin, which
			// is only the screen's center while the camera looks straight at it.
			expect(topPercent).toBeCloseTo(
				legacyPercent(
					viewport,
					rig.halfHeight,
					placement.localSeatZ - rig.centerZ,
					viewport.height
				),
				5
			);
		});
	}

	it("scales linearly with the frustum's half-width/half-height", () => {
		const rig = computeCameraRig(landscape, 9);
		const near = worldToScreenPercent(rig, rig.halfWidth / 2, 0);
		const far = worldToScreenPercent(rig, rig.halfWidth, 0);
		expect(far.leftPercent - 50).toBeCloseTo((near.leftPercent - 50) * 2, 5);
	});
});
