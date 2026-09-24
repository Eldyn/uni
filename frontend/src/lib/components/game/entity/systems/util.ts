/**
 * @file Shared pure math helpers for entity systems. No runes, no randomness
 * that could leak into game state (deterministic sine "noise" only).
 */
import type { Entity, Vec3 } from "../registry";

/** Resolve a synthetic anchor address against the positions the runtime
 *  injected into `state.anchors` this tick; undefined when missing. */
export function readAnchor(e: Entity, address: unknown): Vec3 | undefined {
	if (typeof address !== "string") return undefined;
	const anchors = e.state.anchors as Record<string, Vec3> | undefined;
	const position = anchors?.[address];
	return Array.isArray(position) ? position : undefined;
}

export function readNumber(value: unknown, fallback: number): number {
	return typeof value === "number" && Number.isFinite(value) ? value : fallback;
}

/** Coerce an unknown into a Vec3, falling back when it is not a 3-tuple. */
export function readVec3(value: unknown, fallback: Vec3): Vec3 {
	if (Array.isArray(value) && value.length === 3 && value.every((n) => typeof n === "number")) {
		return [value[0] as number, value[1] as number, value[2] as number];
	}
	return [...fallback] as Vec3;
}

export function addVec3(a: Vec3, b: Vec3): Vec3 {
	return [a[0] + b[0], a[1] + b[1], a[2] + b[2]];
}

export function subVec3(a: Vec3, b: Vec3): Vec3 {
	return [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
}

export function scaleVec3(a: Vec3, s: number): Vec3 {
	return [a[0] * s, a[1] * s, a[2] * s];
}

export function lerpVec3(a: Vec3, b: Vec3, t: number): Vec3 {
	return [a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t];
}

export function lengthVec3(a: Vec3): number {
	return Math.hypot(a[0], a[1], a[2]);
}

/** Axis index for the schema's axis strings ("x" | "y" | "z"), defaulting to y. */
export function axisIndex(axis: unknown): 0 | 1 | 2 {
	if (axis === "x") return 0;
	if (axis === "z") return 2;
	return 1;
}

/** Set one axis of a Vec3, returning a fresh tuple. */
export function withAxis(a: Vec3, axis: unknown, value: number): Vec3 {
	const next: Vec3 = [a[0], a[1], a[2]];
	next[axisIndex(axis)] = value;
	return next;
}
