import type { SeatPosition3D } from "./seatLayout3D";

export const SPIN_SECONDS = 0.5;
export const INHERIT_SECONDS = 0.35;
export const SPIN_EASE = "power2.inOut";
export const INHERIT_EASE = "power2.out";

const MIN_SPEED_MULTIPLIER = 0.1;
const TAU = Math.PI * 2;

export interface SlotPose {
	x: number;
	z: number;
	rotationY: number;
}

function shortestAngle(delta: number): number {
	let a = delta % TAU;
	if (a > Math.PI) a -= TAU;
	if (a < -Math.PI) a += TAU;
	return a;
}

/**
 * Signed shortest number of slots that rotates `to` onto slot 0 of `order`.
 * Positive steps move seats toward lower slot indices. Equally-short ties
 * break toward `lastDirection` (clockwise on a cold start).
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

/** Slot 0 is the bottom pivot; slots 1..N-1 follow the arch seats. */
export function buildSlotSequence(
	seats3D: readonly SeatPosition3D[],
	bottomPose: SeatPosition3D
): SeatPosition3D[] {
	return [bottomPose, ...seats3D];
}

/** Pose between two slots, rotating about the mat center (world origin). */
export function interpolateSlotPath(from: SeatPosition3D, to: SeatPosition3D, t: number): SlotPose {
	const rFrom = Math.hypot(from.x, from.z);
	const rTo = Math.hypot(to.x, to.z);
	const rotationY = from.rotationY + shortestAngle(to.rotationY - from.rotationY) * t;
	if (rFrom === 0 && rTo === 0) return { x: 0, z: 0, rotationY };
	const aFrom = rFrom === 0 ? Math.atan2(to.z, to.x) : Math.atan2(from.z, from.x);
	const aTo = rTo === 0 ? Math.atan2(from.z, from.x) : Math.atan2(to.z, to.x);
	const angle = aFrom + shortestAngle(aTo - aFrom) * t;
	const radius = rFrom + (rTo - rFrom) * t;
	return { x: radius * Math.cos(angle), z: radius * Math.sin(angle), rotationY };
}

/** Signed angle the incoming seat sweeps to the bottom pivot, about origin. */
export function spinAngleBetween(fromSeat: SeatPosition3D, bottomPose: SeatPosition3D): number {
	const aFrom = Math.atan2(fromSeat.z, fromSeat.x);
	const aTo = Math.atan2(bottomPose.z, bottomPose.x);
	return shortestAngle(aTo - aFrom);
}

export function spinDurations(
	speedMultiplier: number,
	enabled: boolean
): { spin: number; inherit: number } {
	if (!enabled) return { spin: 0, inherit: 0 };
	const m = Math.max(MIN_SPEED_MULTIPLIER, speedMultiplier);
	return { spin: SPIN_SECONDS / m, inherit: INHERIT_SECONDS / m };
}
