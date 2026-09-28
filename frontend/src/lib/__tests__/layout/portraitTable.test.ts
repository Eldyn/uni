import { describe, it, expect } from "vitest";

import { computeSceneGeometry } from "$components/game/layout/sceneGeometry";
import { opponentSeatReachWorld } from "$components/game/layout/handRing";
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

	it("centers a sparse table's felt on the spin pivot", () => {
		const { placement } = computeSceneGeometry(phones[0], 3);
		const { far, near } = placement.mat.bounds;
		expect((far + near) / 2).toBeCloseTo(0, 5);
	});
});
