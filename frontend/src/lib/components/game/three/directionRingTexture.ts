/**
 * @file directionRingTexture.ts
 * @brief Canvas-drawn ring of evenly spaced, hard-edged pixel chevrons
 * pointing counter-clockwise on screen (forward play: the next seat is due
 * right of the local player), echoing the open arrowhead of the mat's chalk
 * loop, for the table's play-direction ring. White; tinted per use.
 */

import { CanvasTexture, NearestFilter, SRGBColorSpace } from "three";

const CANVAS_SIZE = 256;
const CHEVRON_COUNT = 12;
/** Ring centre line, as a fraction of the canvas half-size. */
const RING_RADIUS = 0.9;
/** Chevron length along the ring and half-spread across it, in texels. */
const CHEVRON_LENGTH = 11;
const CHEVRON_HALF_SPREAD = 8;
const STROKE_WIDTH = 3;
/** Dashed track between chevrons, like the dashed leg of the mat's loop. */
const DASH_LENGTH = 5;
const DASH_PITCH = 10;
const DASH_HALF_WIDTH = 1.5;
const DASH_CLEARANCE = CHEVRON_LENGTH;
const OPAQUE = 255;

let cached: CanvasTexture | null = null;

/** Distance from (px, py) to the segment (ax, ay)-(bx, by). */
function segmentDistance(
	px: number,
	py: number,
	ax: number,
	ay: number,
	bx: number,
	by: number
): number {
	const abx = bx - ax;
	const aby = by - ay;
	const t = Math.max(0, Math.min(1, ((px - ax) * abx + (py - ay) * aby) / (abx * abx + aby * aby)));
	return Math.hypot(px - ax - abx * t, py - ay - aby * t);
}

/** True when the texel at (along, across), in the frame of the nearest
 *  chevron (along = arc length counter-clockwise, across = outward), is inked. */
function onChevron(along: number, across: number): boolean {
	const apex = CHEVRON_LENGTH / 2;
	const tail = -CHEVRON_LENGTH / 2;
	const upperArm = segmentDistance(along, across, tail, CHEVRON_HALF_SPREAD, apex, 0);
	const lowerArm = segmentDistance(along, across, tail, -CHEVRON_HALF_SPREAD, apex, 0);
	return Math.min(upperArm, lowerArm) <= STROKE_WIDTH / 2;
}

/** True when the texel sits on a dash of the track between two chevrons. */
function onTrack(along: number, across: number): boolean {
	if (Math.abs(along) < DASH_CLEARANCE || Math.abs(across) > DASH_HALF_WIDTH) return false;
	const dashPhase = (((along - DASH_CLEARANCE) % DASH_PITCH) + DASH_PITCH) % DASH_PITCH;
	return dashPhase < DASH_LENGTH;
}

/** Alpha per texel (row-major, 0 or 255) of a `size`-square ring. Canvas
 *  angles grow clockwise on screen, so each apex leads against them. */
export function directionRingAlpha(size: number = CANVAS_SIZE): Uint8Array {
	const alpha = new Uint8Array(size * size);
	const center = size / 2;
	const ringRadius = center * RING_RADIUS;
	const cellAngle = (Math.PI * 2) / CHEVRON_COUNT;
	for (let y = 0; y < size; y++) {
		for (let x = 0; x < size; x++) {
			const dx = x + 0.5 - center;
			const dy = y + 0.5 - center;
			const angle = Math.atan2(dy, dx);
			const nearestCell = Math.round(angle / cellAngle) * cellAngle;
			const along = (nearestCell - angle) * ringRadius;
			const across = Math.hypot(dx, dy) - ringRadius;
			if (onChevron(along, across) || onTrack(along, across)) alpha[y * size + x] = OPAQUE;
		}
	}
	return alpha;
}

/** Returns the shared chevron ring, or null when there is no DOM. */
export function directionRingTexture(): CanvasTexture | null {
	if (cached) return cached;
	if (typeof document === "undefined") return null;

	const canvas = document.createElement("canvas");
	canvas.width = CANVAS_SIZE;
	canvas.height = CANVAS_SIZE;
	const ctx = canvas.getContext("2d");
	if (!ctx) return null;

	const alpha = directionRingAlpha(CANVAS_SIZE);
	const image = ctx.createImageData(CANVAS_SIZE, CANVAS_SIZE);
	for (let texel = 0; texel < alpha.length; texel++) {
		const offset = texel * 4;
		image.data[offset] = 255;
		image.data[offset + 1] = 255;
		image.data[offset + 2] = 255;
		image.data[offset + 3] = alpha[texel];
	}
	ctx.putImageData(image, 0, 0);

	const texture = new CanvasTexture(canvas);
	texture.colorSpace = SRGBColorSpace;
	texture.magFilter = NearestFilter;
	texture.minFilter = NearestFilter;
	cached = texture;
	return cached;
}
