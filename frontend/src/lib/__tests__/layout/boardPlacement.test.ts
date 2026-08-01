import { describe, it, expect } from "vitest";

import { computeCameraRig } from "$components/game/layout/cameraRig";
import {
	computeBoardPlacement,
	LOCAL_AVATAR_WORLD,
	LOCAL_SEAT_GAP,
	MAX_HAND_SCALE,
	HAND_BOTTOM_MARGIN,
	DRAW_PILE_HOME_X,
	DRAW_PILE_EDGE_MARGIN,
	MIN_CENTER_SCALE,
	CENTER_RING_MARGIN
} from "$components/game/layout/boardPlacement";
import { matBounds } from "$components/game/layout/playmat";
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

	// The hand is sized by the strip of table between the felt's near edge and
	// the screen's bottom: fill it, and the player's own cards are as big as the
	// board can afford without any part of the avatar climbing onto the felt.
	it("sizes the hand so the local avatar's far edge comes to rest on the felt's near edge", () => {
		for (const [name, viewport] of Object.entries(all)) {
			const { rig, placement } = placementFor(viewport);
			if (placement.handScale >= MAX_HAND_SCALE) continue; // bounded, see below
			const avatarFarZ = placement.localAvatarZ - LOCAL_AVATAR_WORLD / 2;
			expect(avatarFarZ, name).toBeCloseTo(matBounds(rig.halfWidth, rig.halfHeight).near, 5);
		}
	});

	it("keeps one seat gap between the avatar and the hand row", () => {
		const { placement } = placementFor(wide);
		const handFarZ = placement.localSeatZ - (CARD_HEIGHT * placement.handScale) / 2;
		expect(handFarZ - placement.localAvatarZ).toBeCloseTo(LOCAL_SEAT_GAP, 5);
	});

	it("bounds the hand so an extreme aspect can't blow it up without bound", () => {
		const sliver: ViewportInfo = { width: 200, height: 2000, orientation: "portrait" };
		const { placement } = placementFor(sliver);
		expect(placement.handScale).toBeCloseTo(MAX_HAND_SCALE, 5);
	});

	// The card you're about to play and the card it lands on are the same object
	// as far as the player is concerned, so they're drawn the same size.
	it("matches the discard pile to the hand's own cards", () => {
		for (const [name, viewport] of Object.entries(all)) {
			const { placement } = placementFor(viewport);
			expect(placement.centerScale, name).toBeCloseTo(placement.handScale, 5);
		}
	});

	it("shrinks the center pile only once a seat is close enough to collide", () => {
		const rig = computeCameraRig(wide, 9);
		const roomy = computeBoardPlacement(wide, rig, 4);
		expect(roomy.centerScale).toBeCloseTo(roomy.handScale, 5);

		const cramped = computeBoardPlacement(wide, rig, 1.2).centerScale;
		expect(cramped).toBeLessThan(roomy.handScale);
		expect(cramped).toBeGreaterThanOrEqual(MIN_CENTER_SCALE);
		// Whatever it shrank to, the pile still has to fit the clearance it was
		// given, margin included.
		expect((CARD_HEIGHT * cramped) / 2).toBeLessThanOrEqual(1.2 - CENTER_RING_MARGIN + 1e-6);
	});

	it("never shrinks the center pile past its readable floor", () => {
		const rig = computeCameraRig(wide, 9);
		expect(computeBoardPlacement(wide, rig, 0).centerScale).toBeCloseTo(MIN_CENTER_SCALE, 5);
	});
});
