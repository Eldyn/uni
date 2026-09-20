/**
 * @file seatRingPerspective.ts
 * @brief Perspective-correct placement for an OPPONENT seat's hand ring and
 * its overhead marker (avatar + name label).
 *
 * Before the board switched from an orthographic top-down camera to a tilted
 * perspective one, a seat's hand ring could be authored as a plain world-space
 * circle on the table plane, and its avatar/name sprites simply lifted straight
 * up off that circle's center — parallel projection made both read as centred
 * and even. Under perspective that breaks in two ways:
 *
 *  1. A sprite lifted straight up no longer projects onto the circle's centre;
 *     for an off-centre seat the drift is a visible horizontal shift (the name
 *     reads off-centre over the ring).
 *  2. A world-space circle on the table projects to a slightly different
 *     ellipse depending on where it sits relative to the camera, so the arc of
 *     card backs reads subtly curved/askew rather than as a clean fan.
 *
 * This module solves both by treating the seat as what it actually is — a
 * screen-space UI gadget laid over the 3D table. The ring is specified as a
 * circle in screen space centred on the seat's projected ground point, and each
 * slot is unprojected back onto the table plane at its own stack height; the
 * avatar/name are placed along the camera ray so they land exactly on that same
 * anchor. Cards keep lying flat on the table (only the ring's world shape
 * becomes the ellipse whose projection is the intended screen circle), so no
 * new pose channels are needed.
 *
 * Pure (no Svelte, no DOM): it builds the same `THREE.PerspectiveCamera` Scene3D
 * mounts from the shared `CameraRig`, so it stays unit-testable and both the
 * renderer (PlayerSeat3D) and the animation anchor resolver (GameBoard's
 * `getOpponentCardPose`) can call the identical function.
 */

import { PerspectiveCamera, Vector3 } from "three";
import type { CameraRig } from "./cameraRig";
import type { ViewportInfo } from "./seatLayout";
import type { SeatPosition3D } from "./seatLayout3D";
import { computeHandRingSlots, opponentRingRadiusWorld, RING_STACK_STEP } from "./handRing";

/** One opponent ring card's world placement, solved against the camera. */
export interface OpponentRingPose {
	position: [number, number, number];
	/** In-plane spin, degrees, so the card reads radial on screen. */
	spinDeg: number;
	/** World scale, compensated so the card's apparent size is uniform. */
	scale: number;
}

/**
 * Builds the perspective camera Scene3D mounts, from the shared rig. Kept in
 * lockstep with Scene3D's `<T.PerspectiveCamera>` wiring (fov, aspect, near,
 * far, position, look-at) — that component and this module must never disagree
 * about the projection, or the solved ring would land off its own camera.
 */
export function rigCamera(rig: CameraRig, viewport: ViewportInfo): PerspectiveCamera {
	const camera = new PerspectiveCamera(
		rig.fov,
		viewport.width / viewport.height,
		rig.near,
		rig.far
	);
	camera.position.set(...rig.position);
	camera.lookAt(...rig.lookAt);
	camera.updateMatrixWorld();
	camera.updateProjectionMatrix();
	return camera;
}

function projectPx(
	camera: PerspectiveCamera,
	viewport: ViewportInfo,
	x: number,
	y: number,
	z: number
): { x: number; y: number } {
	const projected = new Vector3(x, y, z).project(camera);
	return {
		x: (projected.x * 0.5 + 0.5) * viewport.width,
		y: (-projected.y * 0.5 + 0.5) * viewport.height
	};
}

/**
 * The world point on the horizontal plane `planeY` that projects to the given
 * screen pixel — the inverse of `projectPx`. Used to pin an elevated sprite
 * (or a ring card) onto a screen position while keeping it at its own height.
 */
function unprojectToPlaneY(
	camera: PerspectiveCamera,
	viewport: ViewportInfo,
	screenX: number,
	screenY: number,
	planeY: number
): { x: number; z: number } {
	const onRay = new Vector3(
		(screenX / viewport.width) * 2 - 1,
		-(screenY / viewport.height) * 2 + 1,
		0.5
	).unproject(camera);
	const origin = camera.position;
	const dir = onRay.clone().sub(origin).normalize();
	const t = (planeY - origin.y) / dir.y;
	return { x: origin.x + dir.x * t, z: origin.z + dir.z * t };
}

/** Camera-axis depth of a world point (distance along the view direction). */
function axisDepth(camera: PerspectiveCamera, x: number, y: number, z: number): number {
	const local = new Vector3(x, y, z).applyMatrix4(camera.matrixWorldInverse);
	return -local.z;
}

/**
 * Local X/Z offset (in the seat group's own rotated frame) at which a sprite
 * of world height `height` projects exactly onto the seat's table-plane origin.
 * Applying it to the avatar/name sprites is what re-centres them over the ring
 * under the tilted camera.
 */
export function computeSeatMarkerOffset(
	rig: CameraRig,
	viewport: ViewportInfo,
	seat: Pick<SeatPosition3D, "x" | "z" | "rotationY">,
	height: number
): [number, number] {
	const camera = rigCamera(rig, viewport);
	const anchor = projectPx(camera, viewport, seat.x, 0, seat.z);
	const world = unprojectToPlaneY(camera, viewport, anchor.x, anchor.y, height);
	const worldX = world.x - seat.x;
	const worldZ = world.z - seat.z;
	const cos = Math.cos(seat.rotationY);
	const sin = Math.sin(seat.rotationY);
	// Inverse of handRing's local→world rotation, so the offset can be applied
	// as a position inside the seat's rotated <T.Group>.
	return [worldX * cos - worldZ * sin, worldX * sin + worldZ * cos];
}

/**
 * Solves one opponent's whole hand ring against the perspective camera.
 *
 * The ring is defined in screen space: a circle of the seat's radius, centred
 * on the seat's projected ground point, with the same per-slot angles
 * `computeHandRingSlots` produces (so a small hand still reads as a fan centred
 * on the line to the mat, and a full hand as a closed ring). Each slot's screen
 * point is then unprojected onto the table plane at its own stack height, so
 * the projected result is exactly the intended circle — no perspective ellipse,
 * no drift from the lifted avatar.
 */
export function computeOpponentRingPoses(
	rig: CameraRig,
	viewport: ViewportInfo,
	seat: Pick<SeatPosition3D, "x" | "z" | "rotationY">,
	avatarWorld: number,
	cardScale: number,
	cardCount: number,
	stackStep: number = RING_STACK_STEP
): OpponentRingPose[] {
	const slots = computeHandRingSlots(cardCount);
	if (slots.length === 0) return [];

	const camera = rigCamera(rig, viewport);
	const anchor = projectPx(camera, viewport, seat.x, 0, seat.z);
	const cos = Math.cos(seat.rotationY);
	const sin = Math.sin(seat.rotationY);

	// Screen radius, measured in the direction of the ring's own radius (toward
	// the mat centre), so the fan keeps the clearance from the avatar it was
	// laid out for. Under the camera this is very nearly the seat's depth.
	const radiusWorld = opponentRingRadiusWorld(avatarWorld, cardScale);
	const radiusProbe = projectPx(
		camera,
		viewport,
		seat.x + sin * radiusWorld,
		0,
		seat.z + cos * radiusWorld
	);
	const screenRadius = Math.hypot(radiusProbe.x - anchor.x, radiusProbe.y - anchor.y);

	const seatDepth = axisDepth(camera, seat.x, 0, seat.z);
	const baseSpinDeg = (seat.rotationY * 180) / Math.PI + 180;

	return slots.map((slot, i) => {
		// This slot's radial direction, in the seat's local frame (angle 0 points
		// at the mat centre), rotated into world.
		const angleRad = (slot.rotateDeg * Math.PI) / 180;
		const localX = Math.sin(angleRad);
		const localZ = Math.cos(angleRad);
		const dirX = localX * cos + localZ * sin;
		const dirZ = -localX * sin + localZ * cos;

		// That direction's screen displacement, so the screen circle is traversed
		// with the same angular order as the world slots.
		const probe = projectPx(
			camera,
			viewport,
			seat.x + dirX * radiusWorld,
			0,
			seat.z + dirZ * radiusWorld
		);
		const probeLen = Math.hypot(probe.x - anchor.x, probe.y - anchor.y) || 1;
		const screenX = anchor.x + ((probe.x - anchor.x) / probeLen) * screenRadius;
		const screenY = anchor.y + ((probe.y - anchor.y) / probeLen) * screenRadius;

		const y = i * stackStep;
		const world = unprojectToPlaneY(camera, viewport, screenX, screenY, y);
		const depth = axisDepth(camera, world.x, y, world.z);

		return {
			position: [world.x, y, world.z],
			spinDeg: baseSpinDeg + slot.rotateDeg,
			// Keep the apparent card size even across the fan: a slot on the near
			// side of the ring is closer to the camera, so its world size is
			// scaled up to match the seat depth.
			scale: cardScale * (depth / seatDepth)
		};
	});
}
