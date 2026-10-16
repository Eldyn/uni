/**
 * @file Motion systems: `motion.spin` / `.oscillate` / `.jitter` /
 * `.spring` / `.follow`. Pure `(entity, dt, params) => statePatch`. Jitter uses
 * deterministic sine "noise" seeded by instanceId — never a cosmetic RNG
 * and never game state.
 */
import type { EntityState, SystemFn, Vec3 } from "../registry";
import {
	addVec3,
	lerpVec3,
	readAnchor,
	readNumber,
	readVec3,
	scaleVec3,
	subVec3,
	withAxis
} from "./util";

export const motionSpin: SystemFn = (e, dt, params): EntityState => {
	const p = (params ?? {}) as { axis?: string; turns?: number; hz?: number };
	const angle =
		readNumber(e.state.angle, 0) + readNumber(p.turns, 0) * 2 * Math.PI * readNumber(p.hz, 0) * dt;
	return { angle, spinAxis: p.axis ?? "y" };
};

export const motionOscillate: SystemFn = (e, dt, params): EntityState => {
	const p = (params ?? {}) as { axis?: string; amplitude?: number; hz?: number };
	const age = readNumber(e.state.motionAge, 0) + dt;
	const value = readNumber(p.amplitude, 0) * Math.sin(2 * Math.PI * readNumber(p.hz, 0) * age);
	const offset = readVec3(e.state.offset, [0, 0, 0]);
	return { offset: withAxis(offset, p.axis, value), motionAge: age };
};

export const motionJitter: SystemFn = (e, dt, params): EntityState => {
	const p = (params ?? {}) as { amplitude?: number; hz?: number };
	const age = readNumber(e.state.motionAge, 0) + dt;
	const amplitude = readNumber(p.amplitude, 0);
	const hz = readNumber(p.hz, 0);
	const seed = e.instanceId * 12.9898;
	const offset: Vec3 = [
		Math.sin(age * hz * 311 + seed) * amplitude,
		Math.sin(age * hz * 157 + seed * 1.7) * amplitude,
		Math.sin(age * hz * 191 + seed * 2.3) * amplitude
	];
	return { offset, motionAge: age };
};

export const motionSpring: SystemFn = (e, dt, params): EntityState => {
	const p = (params ?? {}) as { target?: string; stiffness?: number; damping?: number };
	const target = readAnchor(e, p.target);
	if (!target) return {};
	const stiffness = Math.max(readNumber(p.stiffness, 1), 1e-4);
	const damping = readNumber(p.damping, 0) || 2 * Math.sqrt(stiffness);
	const position = readVec3(e.state.position, [0, 0, 0]);
	const velocity = readVec3(e.state.velocity, [0, 0, 0]);
	const accel = subVec3(
		scaleVec3(subVec3(target, position), stiffness),
		scaleVec3(velocity, damping)
	);
	const nextVelocity = addVec3(velocity, scaleVec3(accel, dt));
	const nextPosition = addVec3(position, scaleVec3(nextVelocity, dt));
	return { position: nextPosition, velocity: nextVelocity };
};

export const motionFollow: SystemFn = (e, dt, params): EntityState => {
	const p = (params ?? {}) as { target?: string; strength?: number };
	const target = readAnchor(e, p.target);
	if (!target) return {};
	const position = readVec3(e.state.position, [0, 0, 0]);
	const alpha = Math.min(Math.max(readNumber(p.strength, 0), 0) * dt, 1);
	return { position: lerpVec3(position, target, alpha) };
};

export const motionSystems: Record<string, SystemFn> = {
	"motion.spin": motionSpin,
	"motion.oscillate": motionOscillate,
	"motion.jitter": motionJitter,
	"motion.spring": motionSpring,
	"motion.follow": motionFollow
};
