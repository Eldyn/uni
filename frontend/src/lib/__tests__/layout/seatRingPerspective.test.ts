import { describe, it, expect } from "vitest";
import { Vector3 } from "three";

import {
	computeOpponentRingPoses,
	computeSeatMarkerOffset,
	rigCamera
} from "$components/game/layout/seatRingPerspective";
import { computeSceneGeometry } from "$components/game/layout/sceneGeometry";
import { opponentRingRadiusWorld } from "$components/game/layout/handRing";
import { worldToScreenPercent } from "$components/game/layout/screenProjection";
import type { ViewportInfo } from "$components/game/layout/seatLayout";

const viewports: Record<string, ViewportInfo> = {
	landscape: { width: 1600, height: 1000, orientation: "landscape" },
	wide: { width: 1920, height: 1080, orientation: "landscape" },
	portrait: { width: 390, height: 844, orientation: "portrait" }
};

function px(
	rig: ReturnType<typeof computeSceneGeometry>["rig"],
	vp: ViewportInfo,
	x: number,
	y: number,
	z: number
) {
	const camera = rigCamera(rig, vp);
	const p = new Vector3(x, y, z).project(camera);
	return { x: (p.x * 0.5 + 0.5) * vp.width, y: (-p.y * 0.5 + 0.5) * vp.height };
}

describe("computeOpponentRingPoses", () => {
	for (const [name, viewport] of Object.entries(viewports)) {
		it(`projects every ring slot onto a circle centred on the seat (${name})`, () => {
			const geom = computeSceneGeometry(viewport, 9);
			const seat = geom.seats3D[0];
			const poses = computeOpponentRingPoses(
				geom.rig,
				viewport,
				seat,
				geom.opponentAvatarWorld,
				geom.opponentCardScale,
				13
			);
			expect(poses).toHaveLength(13);

			const anchor = px(geom.rig, viewport, seat.x, 0, seat.z);
			// Same radius the solver measures along the line to the mat centre.
			const radiusWorld = opponentRingRadiusWorld(geom.opponentAvatarWorld, geom.opponentCardScale);
			const cos = Math.cos(seat.rotationY);
			const sin = Math.sin(seat.rotationY);
			const radiusProbe = px(
				geom.rig,
				viewport,
				seat.x + sin * radiusWorld,
				0,
				seat.z + cos * radiusWorld
			);
			const screenRadius = Math.hypot(radiusProbe.x - anchor.x, radiusProbe.y - anchor.y);

			for (const pose of poses) {
				const p = px(geom.rig, viewport, pose.position[0], pose.position[1], pose.position[2]);
				const dist = Math.hypot(p.x - anchor.x, p.y - anchor.y);
				expect(dist).toBeCloseTo(screenRadius, 3);
			}
		});
	}

	it("keeps the ring's angular order and starts toward the mat centre", () => {
		const viewport = viewports.landscape;
		const geom = computeSceneGeometry(viewport, 9);
		const seat = geom.seats3D[0];
		const poses = computeOpponentRingPoses(
			geom.rig,
			viewport,
			seat,
			geom.opponentAvatarWorld,
			geom.opponentCardScale,
			5
		);
		const anchor = px(geom.rig, viewport, seat.x, 0, seat.z);
		const towards = px(geom.rig, viewport, 0, 0, 0);
		// Slot 2 is the middle of 5 (rotateDeg 0) — it must sit on the anchor →
		// mat-centre screen line.
		const mid = px(
			geom.rig,
			viewport,
			poses[2].position[0],
			poses[2].position[1],
			poses[2].position[2]
		);
		const cross =
			(mid.x - anchor.x) * (towards.y - anchor.y) - (mid.y - anchor.y) * (towards.x - anchor.x);
		expect(Math.abs(cross)).toBeLessThan(1);
	});

	it("compensates scale so the fan reads a uniform size", () => {
		const viewport = viewports.landscape;
		const geom = computeSceneGeometry(viewport, 9);
		const seat = geom.seats3D[0];
		const poses = computeOpponentRingPoses(
			geom.rig,
			viewport,
			seat,
			geom.opponentAvatarWorld,
			geom.opponentCardScale,
			13
		);
		// Apparent (screen) size is world scale / camera depth, so the world
		// scale must rise with depth. The near and far slots therefore differ in
		// world scale.
		const scales = poses.map((p) => p.scale);
		expect(Math.max(...scales)).toBeGreaterThan(Math.min(...scales));
	});
});

describe("computeSeatMarkerOffset", () => {
	for (const [name, viewport] of Object.entries(viewports)) {
		it(`lands the lifted marker back on the seat's ground point (${name})`, () => {
			const geom = computeSceneGeometry(viewport, 9);
			const seat = geom.seats3D[0];
			const height = 0.9;
			const [localX, localZ] = computeSeatMarkerOffset(geom.rig, viewport, seat, height);
			// Local offset → world, the same transform the seat's group applies.
			const cos = Math.cos(seat.rotationY);
			const sin = Math.sin(seat.rotationY);
			const worldX = seat.x + (localX * cos + localZ * sin);
			const worldZ = seat.z + (-localX * sin + localZ * cos);
			const lifted = worldToScreenPercent(rigCamera(geom.rig, viewport), worldX, height, worldZ);
			const ground = worldToScreenPercent(rigCamera(geom.rig, viewport), seat.x, 0, seat.z);
			expect(lifted.leftPercent).toBeCloseTo(ground.leftPercent, 5);
			expect(lifted.topPercent).toBeCloseTo(ground.topPercent, 5);
		});
	}
});
