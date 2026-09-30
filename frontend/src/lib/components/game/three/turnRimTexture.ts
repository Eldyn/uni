/**
 * @file turnRimTexture.ts
 * @brief Canvas-drawn soft ring with ordered (Bayer) dithered alpha, used as
 * the local seat's turn rim. Tinted per use via material colour; cached.
 */

import { CanvasTexture, NearestFilter, SRGBColorSpace } from "three";

const CANVAS_SIZE = 128;
const RING_INNER = 0.62;
const RING_OUTER = 0.98;
const BAYER_SIZE = 4;
const BAYER_LEVELS = 4;
// prettier-ignore
const BAYER_MATRIX = [
	0, 8, 2, 10,
	12, 4, 14, 6,
	3, 11, 1, 9,
	15, 7, 13, 5
];

let cached: CanvasTexture | null = null;

function ringIntensity(distance: number): number {
	if (distance < RING_INNER || distance > RING_OUTER) return 0;
	const mid = (RING_INNER + RING_OUTER) / 2;
	const half = (RING_OUTER - RING_INNER) / 2;
	return 1 - Math.abs(distance - mid) / half;
}

/** Returns the shared dithered ring, or null when there is no DOM. */
export function turnRimTexture(): CanvasTexture | null {
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
			const distance = Math.hypot(x - center, y - center) / center;
			const threshold = (BAYER_MATRIX[(y % BAYER_SIZE) * BAYER_SIZE + (x % BAYER_SIZE)] + 0.5) / 16;
			const quantized = Math.floor(ringIntensity(distance) * BAYER_LEVELS + threshold);
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
