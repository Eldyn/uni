/**
 * @file Movement systems: `movement.static` / `.linear` / `.arc` /
 * `.ballistic`. Pure `(entity, dt, params) => statePatch`. Anchor addresses are
 * resolved by the runtime and injected into `state.anchors`; a missing anchor
 * degrades (position unchanged) rather than throwing.
 */
import type { EntityState, SystemFn, Vec3 } from "../registry";
import {
	addVec3,
	lengthVec3,
	lerpVec3,
	readAnchor,
	readNumber,
	readVec3,
	scaleVec3,
	subVec3
} from "./util";

/** Arc/linear have no schema duration; `duration` is an optional extra key
 *  (the schema is `additionalProperties: true`) and defaults here. */
export const DEFAULT_MOVE_DURATION_S = 1;

export const moveStatic: SystemFn = (): EntityState => ({});

export const moveLinear: SystemFn = (e, dt, params): EntityState => {
	const p = (params ?? {}) as { from?: string; to?: string; duration?: number };
	const elapsed = readNumber(e.state.moveElapsed, 0) + dt;
	const from = readAnchor(e, p.from);
	const to = readAnchor(e, p.to);
	if (!from || !to) return { moveElapsed: elapsed };
	const duration = Math.max(readNumber(p.duration, DEFAULT_MOVE_DURATION_S), 1e-6);
	const t = Math.min(elapsed / duration, 1);
	return { position: lerpVec3(from, to, t), moveElapsed: elapsed };
};

export const moveArc: SystemFn = (e, dt, params): EntityState => {
	const p = (params ?? {}) as { from?: string; to?: string; height?: number; duration?: number };
	const elapsed = readNumber(e.state.moveElapsed, 0) + dt;
	const from = readAnchor(e, p.from);
	const to = readAnchor(e, p.to);
	if (!from || !to) return { moveElapsed: elapsed };
	const height = readNumber(p.height, 0);
	const apex: Vec3 = [(from[0] + to[0]) / 2, (from[1] + to[1]) / 2 + height, (from[2] + to[2]) / 2];
	const duration = Math.max(readNumber(p.duration, DEFAULT_MOVE_DURATION_S), 1e-6);
	const t = Math.min(elapsed / duration, 1);
	const position = t < 0.5 ? lerpVec3(from, apex, t * 2) : lerpVec3(apex, to, (t - 0.5) * 2);
	return { position, moveElapsed: elapsed };
};

export const moveBallistic: SystemFn = (e, dt, params): EntityState => {
	const p = (params ?? {}) as {
		from?: string;
		to?: string;
		speed?: number;
		gravity?: number;
	};
	const elapsed = readNumber(e.state.moveElapsed, 0) + dt;
	const from = readAnchor(e, p.from);
	const to = readAnchor(e, p.to);
	if (!from || !to) return { moveElapsed: elapsed };
	const speed = readNumber(p.speed, 1);
	const gravity = readNumber(p.gravity, 0);
	const started = e.state.moveStarted === true;
	let position = started ? readVec3(e.state.position, from) : from;
	let velocity = started ? readVec3(e.state.velocity, [0, 0, 0]) : null;
	if (!velocity) {
		const direction = subVec3(to, from);
		const length = lengthVec3(direction) || 1;
		velocity = scaleVec3(direction, speed / length);
	}
	velocity = [velocity[0], velocity[1] - gravity * dt, velocity[2]];
	position = addVec3(position, scaleVec3(velocity, dt));
	return { position, velocity, moveElapsed: elapsed, moveStarted: true };
};

export const moveSystems: Record<string, SystemFn> = {
	"movement.static": moveStatic,
	"movement.linear": moveLinear,
	"movement.arc": moveArc,
	"movement.ballistic": moveBallistic
};
