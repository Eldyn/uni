import { describe, it, expect } from "vitest";

import { computeCameraRig } from "$components/game/layout/cameraRig";
import {
	computeBoardPlacement,
	BASE_HAND_SCALE,
	MAX_HAND_BOOST,
	HAND_BOTTOM_MARGIN,
	DRAW_PILE_HOME_X,
	DRAW_PILE_EDGE_MARGIN
} from "$components/game/layout/boardPlacement";
import { CARD_WIDTH, CARD_HEIGHT } from "$components/game/three/units";
import type { ViewportInfo } from "$components/game/layout/seatLayout";

const landscape: ViewportInfo = { width: 1200, height: 800, orientation: "landscape" };
const wide: ViewportInfo = { width: 1920, height: 1080, orientation: "landscape" };
const portrait: ViewportInfo = { width: 390, height: 844, orientation: "portrait" };
const narrowPortrait: ViewportInfo = { width: 360, height: 800, orientation: "portrait" };

const all = { landscape, wide, portrait, narrowPortrait };

function placementFor(viewport: ViewportInfo, opponents = 9) {
	const rig = computeCameraRig(viewport, opponents);
	return { rig, placement: computeBoardPlacement(viewport, rig) };
}

describe("computeBoardPlacement", () => {
	for (const [name, viewport] of Object.entries(all)) {
		it(`drops the hand row exactly one bottom margin inside the frustum (${name})`, () => {
			const { rig, placement } = placementFor(viewport);
			const handFrontZ = placement.localSeatZ + (CARD_HEIGHT * placement.handScale) / 2;
			expect(rig.halfHeight - handFrontZ).toBeCloseTo(HAND_BOTTOM_MARGIN, 5);
		});

		it(`keeps the draw pile fully on screen (${name})`, () => {
			const { rig, placement } = placementFor(viewport);
			// Never further out than the frustum's left edge, never further in than
			// its home X — it only ever gets pulled toward the center, never past it.
			const pileLeftX = placement.drawPileX - (CARD_WIDTH * placement.handScale) / 2;
			expect(pileLeftX).toBeGreaterThanOrEqual(-rig.halfWidth - 1e-6);
			expect(placement.drawPileX).toBeGreaterThanOrEqual(DRAW_PILE_HOME_X - 1e-6);
			expect(placement.drawPileX).toBeLessThan(0);
		});
	}

	it("leaves the draw pile at its home X when the frustum is wide enough", () => {
		const { placement } = placementFor(wide);
		expect(placement.drawPileX).toBeCloseTo(DRAW_PILE_HOME_X, 5);
	});

	it("pulls the draw pile inward on a narrow viewport rather than letting it clip", () => {
		const { rig, placement } = placementFor(narrowPortrait);
		expect(placement.drawPileX).toBeGreaterThan(DRAW_PILE_HOME_X);
		expect(placement.drawPileX).toBeCloseTo(
			-(rig.halfWidth - (CARD_WIDTH * placement.handScale) / 2 - DRAW_PILE_EDGE_MARGIN),
			5
		);
	});

	// A phone's frustum is tall and thin, so the same world-space card reads far
	// smaller than it does on a desktop — the boost buys that size back.
	it("boosts the hand on portrait and leaves landscape at the base scale", () => {
		expect(computeBoardPlacement(wide, computeCameraRig(wide, 9)).handScale).toBeCloseTo(
			BASE_HAND_SCALE,
			5
		);
		expect(placementFor(portrait).placement.handScale).toBeGreaterThan(BASE_HAND_SCALE);
	});

	it("clamps the boost so an extreme aspect can't blow the hand up without bound", () => {
		const sliver: ViewportInfo = { width: 200, height: 2000, orientation: "portrait" };
		const { placement } = placementFor(sliver);
		expect(placement.handScale).toBeCloseTo(BASE_HAND_SCALE * MAX_HAND_BOOST, 5);
	});
});
