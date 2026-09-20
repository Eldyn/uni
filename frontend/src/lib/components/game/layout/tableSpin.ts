import type { SeatPosition3D } from "./seatLayout3D";
import { boardRotationFor } from "./boardRotation";

export const SPIN_SECONDS = 0.5;
export const INHERIT_SECONDS = 0.35;
export const SPIN_EASE = "power2.inOut";
export const INHERIT_EASE = "power2.out";

const MIN_SPEED_MULTIPLIER = 0.1;

export interface SlotPose {
	x: number;
	z: number;
	rotationY: number;
}

function shortestAngle(delta: number): number {
	const TAU = Math.PI * 2;
	let a = delta % TAU;
	if (a > Math.PI) a -= TAU;
	if (a < -Math.PI) a += TAU;
	return a;
}

/**
 * Signed shortest number of slots that rotates `to` onto slot 0 of `order`.
 * Positive steps move seats toward lower slot indices. Equally-short ties
 * break toward `lastDirection` (clockwise on a cold start). Used only to pick
 * the spin's DIRECTION; the angle itself is `boardRotationFor`.
 */
export function spinStepsBetween(
	order: readonly string[],
	from: string,
	to: string,
	lastDirection: 1 | -1 = 1
): number {
	const n = order.length;
	if (n <= 1) return 0;
	const iFrom = order.indexOf(from);
	const iTo = order.indexOf(to);
	if (iFrom < 0 || iTo < 0) return 0;
	const delta = (((iTo - iFrom) % n) + n) % n;
	if (delta === 0) return 0;
	if (delta * 2 < n) return delta;
	if (delta * 2 > n) return delta - n;
	return lastDirection > 0 ? delta : delta - n;
}

/**
 * The signed shortest yaw that brings the incoming seat's own bearing around to
 * the bottom pivot's. Wraps `boardRotationFor` with the ring-order sign so the
 * table always turns the short way, matching `spinStepsBetween`'s direction.
 */
export function spinAngleBetween(fromSeat: SeatPosition3D, bottomPose: SeatPosition3D): number {
	return boardRotationFor(fromSeat, bottomPose);
}

/** Rotates a slot pose about the origin by `yaw` — used by callers that need a
 *  one-off rotated seat position rather than a whole spinning group. */
export function rotateSlotPose(pose: SlotPose, yaw: number): SlotPose {
	const cos = Math.cos(yaw);
	const sin = Math.sin(yaw);
	return {
		x: pose.x * cos + pose.z * sin,
		z: -pose.x * sin + pose.z * cos,
		rotationY: pose.rotationY + yaw
	};
}

export function spinDurations(
	speedMultiplier: number,
	enabled: boolean
): { spin: number; inherit: number } {
	if (!enabled) return { spin: 0, inherit: 0 };
	const m = Math.max(MIN_SPEED_MULTIPLIER, speedMultiplier);
	return { spin: SPIN_SECONDS / m, inherit: INHERIT_SECONDS / m };
}

export { shortestAngle };
