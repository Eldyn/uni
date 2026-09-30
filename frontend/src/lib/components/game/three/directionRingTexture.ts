/**
 * @file directionRingTexture.ts
 * @brief Canvas-drawn ring of clockwise-pointing chevrons with ordered (Bayer)
 * dithered alpha, for the table's play-direction ring. White; tinted per use.
 */

import { CanvasTexture, NearestFilter, SRGBColorSpace } from "three";

const CANVAS_SIZE = 256;
const CHEVRON_COUNT = 14;
const RING_INNER = 0.88;
const RING_OUTER = 0.98;
const CHEVRON_SWEEP = 1.1;
const CHEVRON_THICKNESS = 0.22;
const BAYER_SIZE = 4;
const BAYER_LEVELS = 3;
// prettier-ignore
const BAYER_MATRIX = [
	0, 8, 2, 10,
	12, 4, 14, 6,
	3, 11, 1, 9,
	15, 7, 13, 5
];

let cached: CanvasTexture | null = null;

/** Chevron coverage 0..1 at a polar point. Canvas angles grow clockwise on
 *  screen, and the apex leads in that direction with both arms trailing. */
function chevronIntensity(radius: number, angle: number): number {
	if (radius < RING_INNER || radius > RING_OUTER) return 0;
	const bandHalf = (RING_OUTER - RING_INNER) / 2;
	const bandOffset = Math.abs(radius - (RING_INNER + RING_OUTER) / 2) / bandHalf;
	const cellAngle = (Math.PI * 2) / CHEVRON_COUNT;
	const cellPosition = (((angle % cellAngle) + cellAngle) % cellAngle) / cellAngle - 0.5;
	const strokeDistance = Math.abs(cellPosition + CHEVRON_SWEEP * bandOffset * 0.5 - 0.25);
	return Math.max(0, 1 - strokeDistance / CHEVRON_THICKNESS);
}

/** Returns the shared dithered chevron ring, or null when there is no DOM. */
export function directionRingTexture(): CanvasTexture | null {
	if (cached) return cached;
	if (typeof document === "undefined") return null;

	const canvas = document.createElement("canvas");
	canvas.width = CANVAS_SIZE;
	canvas.height = CANVAS_SIZE;
	const ctx = canvas.getContext("2d");
	if (!ctx) return null;

	const image = ctx.createImageData(CANVAS_SIZE, CANVAS_SIZE);
	const center = CANVAS_SIZE / 2;
	for (let y = 0; y < CANVAS_SIZE; y++) {
		for (let x = 0; x < CANVAS_SIZE; x++) {
			const dx = x - center;
			const dy = y - center;
			const intensity = chevronIntensity(Math.hypot(dx, dy) / center, Math.atan2(dy, dx));
			const threshold = (BAYER_MATRIX[(y % BAYER_SIZE) * BAYER_SIZE + (x % BAYER_SIZE)] + 0.5) / 16;
			const quantized = Math.floor(intensity * BAYER_LEVELS + threshold);
			const offset = (y * CANVAS_SIZE + x) * 4;
			image.data[offset] = 255;
			image.data[offset + 1] = 255;
			image.data[offset + 2] = 255;
			image.data[offset + 3] = quantized > 0 ? 255 : 0;
		}
	}
	ctx.putImageData(image, 0, 0);

	const texture = new CanvasTexture(canvas);
	texture.colorSpace = SRGBColorSpace;
	texture.magFilter = NearestFilter;
	texture.minFilter = NearestFilter;
	cached = texture;
	return cached;
}
