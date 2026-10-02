import { describe, it, expect } from "vitest";

import { computeSceneGeometry } from "$components/game/layout/sceneGeometry";
import { opponentSeatReachWorld } from "$components/game/layout/handRing";
import { PORTRAIT_DRAW_PILE_SCALE } from "$components/game/layout/boardPlacement";
import { PHONE_MAT_SHEET } from "$components/game/layout/playmat";
import { CARD_HEIGHT, EM_TO_WORLD } from "$components/game/three/units";
import { MAX_JITTER_EM } from "$components/game/layout/discardPile";
import { PORTRAIT_HUD_CLEARANCE_PX } from "$components/game/layout/portraitTable";
import { MAX_OPPONENTS, type ViewportInfo } from "$components/game/layout/seatLayout";

const phones: ViewportInfo[] = [
	{ width: 390, height: 844, orientation: "portrait" },
	{ width: 360, height: 740, orientation: "portrait" }
];

describe("portrait table", () => {
	for (const viewport of phones) {
		for (let opponents = 1; opponents <= MAX_OPPONENTS; opponents++) {
			it(`seats every opponent off the felt and on screen (${viewport.width}x${viewport.height}, ${opponents} opponents)`, () => {
				const geometry = computeSceneGeometry(viewport, opponents);
				const { rig, placement } = geometry;
				const felt = placement.mat.bounds;
				const reach = opponentSeatReachWorld(
					geometry.opponentAvatarWorld,
					geometry.opponentCardScale
				);
				const screenTop = rig.centerZ - rig.halfHeight;
				const hudBottom = screenTop + PORTRAIT_HUD_CLEARANCE_PX * geometry.worldPerPx;

				for (const seat of geometry.seats3D) {
					const nearestX = Math.max(felt.left, Math.min(felt.right, seat.x));
					const nearestZ = Math.max(felt.far, Math.min(felt.near, seat.z));
					expect(Math.hypot(seat.x - nearestX, seat.z - nearestZ)).toBeGreaterThan(reach);
					expect(Math.abs(seat.x) + reach).toBeLessThanOrEqual(rig.halfWidth);
					expect(seat.z - reach).toBeGreaterThanOrEqual(hudBottom - 1e-9);
				}
			});
		}

		it(`keeps the felt clear of the local avatar (${viewport.width}x${viewport.height})`, () => {
			for (let opponents = 1; opponents <= MAX_OPPONENTS; opponents++) {
				const { placement } = computeSceneGeometry(viewport, opponents);
				expect(placement.mat.bounds.near).toBeLessThan(placement.localAvatarZ);
			}
		});
	}

	it("tucks the draw pile under the discard at a fraction of the hand size", () => {
		for (const viewport of phones) {
			for (let opponents = 1; opponents <= MAX_OPPONENTS; opponents++) {
				const { placement } = computeSceneGeometry(viewport, opponents);
				expect(placement.drawPileScale).toBeLessThanOrEqual(
					placement.handScale * PORTRAIT_DRAW_PILE_SCALE
				);
				// The front pile must never be as large as the discard behind it.
				expect(placement.drawPileScale).toBeLessThan(placement.centerScale);
				// Draw sits in front of (toward the player from) the discard.
				expect(placement.drawPileZ).toBeGreaterThan(placement.discardZ);
			}
		}
	});

	it("centers a sparse table's felt on the spin pivot", () => {
		const { placement } = computeSceneGeometry(phones[0], 3);
		const { far, near } = placement.mat.bounds;
		expect((far + near) / 2).toBeCloseTo(0, 5);
	});
});

describe("portrait felt", () => {
	const { feltUvRect, texelWidth, texelHeight } = PHONE_MAT_SHEET;
	const feltAspect =
		((feltUvRect.top - feltUvRect.bottom) * texelHeight) /
		((feltUvRect.right - feltUvRect.left) * texelWidth);

	for (const viewport of phones) {
		for (let opponents = 1; opponents <= MAX_OPPONENTS; opponents++) {
			it(`draws the sheet unstretched, upright and centred (${viewport.width}x${viewport.height}, ${opponents} opponents)`, () => {
				const { placement } = computeSceneGeometry(viewport, opponents);
				const { mat } = placement;
				expect(mat.quarterTurn).toBe(false);
				// One scale on both axes: the plane keeps the sheet's own proportions
				// and the felt keeps the proportions it was drawn with.
				expect(mat.size[1] / mat.size[0]).toBeCloseTo(texelHeight / texelWidth, 6);
				const { left, right, far, near } = mat.bounds;
				expect((near - far) / (right - left)).toBeCloseTo(feltAspect, 6);
				expect(left + right).toBeCloseTo(0, 6);
			});

			it(`wraps both piles inside the felt (${viewport.width}x${viewport.height}, ${opponents} opponents)`, () => {
				const { placement } = computeSceneGeometry(viewport, opponents);
				const { left, right, far, near } = placement.mat.bounds;
				const jitter = MAX_JITTER_EM * EM_TO_WORLD;
				const discardReachZ =
					(CARD_HEIGHT / 2) * placement.centerScale + jitter * placement.centerScale;
				const drawReachZ =
					(CARD_HEIGHT / 2) * placement.drawPileScale + jitter * placement.drawPileScale;
				expect(placement.discardZ - discardReachZ).toBeGreaterThan(far);
				expect(placement.drawPileZ + drawReachZ).toBeLessThan(near);
				const reachX = (0.5 + jitter) * placement.centerScale;
				expect(placement.discardX - reachX).toBeGreaterThan(left);
				expect(placement.discardX + reachX).toBeLessThan(right);
			});
		}
	}
});
