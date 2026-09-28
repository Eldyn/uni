import { describe, it, expect } from "vitest";

import { computeCameraRig } from "$components/game/layout/cameraRig";
import {
	computeBoardPlacement,
	LOCAL_AVATAR_WORLD,
	LOCAL_SEAT_GAP,
	MAX_HAND_SCALE,
	MIN_HAND_SCALE,
	HAND_BOTTOM_MARGIN,
	HAND_WIDTH_FILL,
	HAND_MIN_VISIBLE_CARDS,
	HAND_SPACING_RATIO,
	MIN_CENTER_SCALE,
	CENTER_RING_MARGIN,
	CENTER_DISCARD_MARGIN
} from "$components/game/layout/boardPlacement";
import { MAX_JITTER_EM } from "$components/game/layout/discardPile";
import { matBounds } from "$components/game/layout/playmat";
import { CARD_WIDTH, CARD_HEIGHT, EM_TO_WORLD } from "$components/game/three/units";
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
			// Measured from the frustum's near EDGE, which portrait's near-biased
			// camera moves off the origin (see cameraRig.ts's centerZ).
			expect(rig.centerZ + rig.halfHeight - handFrontZ).toBeCloseTo(HAND_BOTTOM_MARGIN, 5);
		});

		it(`keeps the draw pile fully on screen (${name})`, () => {
			const { rig, placement } = placementFor(viewport);
			if (viewport.orientation === "portrait") {
				// Tucked on the discard's own center line, under it.
				expect(placement.drawPileX, name).toBeCloseTo(0, 5);
				return;
			}
			// Landscape: never further out than the frustum's left edge.
			const pileLeftX = placement.drawPileX - (CARD_WIDTH * placement.drawPileScale) / 2;
			expect(pileLeftX, name).toBeGreaterThanOrEqual(-rig.halfWidth - 1e-6);
			expect(placement.drawPileX, name).toBeLessThan(0);
		});
	}

	it("puts both center piles side by side on the mat's center line (landscape)", () => {
		for (const [name, viewport] of Object.entries({ landscape, wide })) {
			const { placement } = placementFor(viewport);
			expect(placement.drawPileZ, name).toBeCloseTo(0, 5);
			expect(placement.discardZ, name).toBeCloseTo(0, 5);
			// Draw on the left, discard on the right.
			expect(placement.drawPileX, name).toBeLessThan(0);
			expect(placement.discardX, name).toBeGreaterThan(0);
			// And far enough apart that their cards cannot overlap.
			const edgeGap = placement.discardX - placement.drawPileX - CARD_WIDTH * placement.handScale;
			expect(edgeGap, name).toBeGreaterThan(0);
		}
	});

	it("keeps the discard's scatter clear of the felt's right edge (landscape)", () => {
		// The pair is deliberately NOT symmetric: the discard's randomly jittered
		// footprint fans right, so it is clamped inward to keep a clearance from
		// the felt edge rather than mirrored to the draw pile's X.
		for (const [name, viewport] of Object.entries({ landscape, wide })) {
			const { placement } = placementFor(viewport);
			const scale = Math.max(placement.centerScale, placement.drawPileScale);
			const reach = (CARD_WIDTH * scale) / 2 + MAX_JITTER_EM * EM_TO_WORLD * scale;
			expect(placement.discardX + reach, name).toBeLessThanOrEqual(
				placement.mat.bounds.right - CENTER_DISCARD_MARGIN + 1e-6
			);
			// Off-centre as a group: the discard sits closer to the centre line
			// than a mirror of the draw pile would.
			expect(placement.discardX, name).toBeLessThan(-placement.drawPileX);
		}
	});

	it("tucks the draw pile under the discard pile in portrait, smaller there", () => {
		for (const [name, viewport] of Object.entries({ portrait, narrowPortrait })) {
			const { placement } = placementFor(viewport);
			// Same center line as the discard, and below it (down-screen, +Z).
			expect(placement.drawPileX, name).toBeCloseTo(0, 5);
			expect(placement.drawPileZ, name).toBeGreaterThan(placement.discardZ);
			// A small tap target: smaller than the hand's own cards.
			expect(placement.drawPileScale, name).toBeLessThan(placement.handScale);
			// Clear of the discard's scatter above it and of the hand row below it.
			const pileFarZ = placement.drawPileZ - (CARD_HEIGHT * placement.drawPileScale) / 2;
			const discardNearZ = placement.discardZ + (CARD_HEIGHT * placement.centerScale) / 2;
			expect(pileFarZ, name).toBeGreaterThan(discardNearZ);
			const pileNearZ = placement.drawPileZ + (CARD_HEIGHT * placement.drawPileScale) / 2;
			const handFarZ = placement.localSeatZ - (CARD_HEIGHT * placement.handScale) / 2;
			expect(pileNearZ, name).toBeLessThan(handFarZ);
		}
		const { placement: landscapePlacement } = placementFor(landscape);
		expect(landscapePlacement.drawPileScale).toBeCloseTo(landscapePlacement.handScale, 5);
	});

	it("centers the discard pile on the felt in every orientation", () => {
		for (const [name, viewport] of Object.entries(all)) {
			const { placement } = placementFor(viewport);
			if (viewport.orientation === "landscape") {
				expect(placement.discardZ, name).toBe(0);
			} else {
				expect(placement.discardZ, name).toBeCloseTo(
					(placement.mat.bounds.far + placement.mat.bounds.near) / 2,
					5
				);
			}
		}
	});

	// The hand is sized by the strip of table between the felt's near edge and
	// the screen's bottom: fill it, and the player's own cards are as big as the
	// board can afford without any part of the avatar climbing onto the felt.
	it("sizes the hand so the local avatar's far edge comes to rest on the felt's near edge", () => {
		for (const [name, viewport] of Object.entries({ landscape, wide })) {
			const { rig, placement } = placementFor(viewport);
			const avatarFarZ = placement.localAvatarZ - LOCAL_AVATAR_WORLD / 2;
			expect(avatarFarZ, name).toBeCloseTo(matBounds(rig.halfWidth, rig.halfHeight).near, 5);
		}
	});

	// Portrait inverts that dependency: the felt is drawn to whatever depth the
	// hand leaves it, so the cards are sized by the row's WIDTH instead — a card
	// solved from a tall phone's vertical strip is one you can only see three of.
	it("keeps a full hand's worth of cards on screen in portrait", () => {
		for (const [name, viewport] of Object.entries({ portrait, narrowPortrait })) {
			const { rig, placement } = placementFor(viewport);
			const rowWidth =
				CARD_WIDTH * placement.handScale * (1 + (HAND_MIN_VISIBLE_CARDS - 1) * HAND_SPACING_RATIO);
			expect(rowWidth, name).toBeLessThanOrEqual(2 * rig.halfWidth * HAND_WIDTH_FILL + 1e-6);
		}
	});

	it("keeps one seat gap between the avatar and the hand row", () => {
		const { placement } = placementFor(wide);
		const handFarZ = placement.localSeatZ - (CARD_HEIGHT * placement.handScale) / 2;
		expect(handFarZ - placement.localAvatarZ).toBeCloseTo(LOCAL_SEAT_GAP, 5);
	});

	it("bounds the hand so an extreme aspect can't blow it up or crush it", () => {
		const slivers = {
			tall: { width: 200, height: 2000, orientation: "portrait" } as ViewportInfo,
			flat: { width: 2000, height: 200, orientation: "landscape" } as ViewportInfo
		};
		for (const [name, viewport] of Object.entries(slivers)) {
			const { placement } = placementFor(viewport);
			expect(placement.handScale, name).toBeGreaterThanOrEqual(MIN_HAND_SCALE);
			expect(placement.handScale, name).toBeLessThanOrEqual(MAX_HAND_SCALE);
		}
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
