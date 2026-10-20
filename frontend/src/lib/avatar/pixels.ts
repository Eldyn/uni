export interface PixelImage {
	width: number;
	height: number;
	data: Uint8ClampedArray; // RGBA
}

/** A fully transparent image of the given size. */
export function createImage(width: number, height: number): PixelImage {
	return { width, height, data: new Uint8ClampedArray(width * height * 4) };
}

/**
 * Clear every `target` pixel whose `mask` alpha is 0. INFO: `target` is clipped
 * in place; a mask of another size leaves it untouched.
 */
export function clipToAlpha(target: PixelImage, mask: PixelImage): void {
	if (target.width !== mask.width || target.height !== mask.height) return;

	for (let offset = 0; offset < target.data.length; offset += 4) {
		if (mask.data[offset + 3] !== 0) continue;

		target.data[offset] = 0;
		target.data[offset + 1] = 0;
		target.data[offset + 2] = 0;
		target.data[offset + 3] = 0;
	}
}

/**
 * Copy a rectangle of `src` onto `dest` at (`destX`, `destY`), source-over.
 * INFO: avatar art is hard-edged, so alpha is only 0 or 255: a pixel either
 * copies through or is left untouched. Out-of-bounds pixels are clipped.
 */
export function blit(
	dest: PixelImage,
	src: PixelImage,
	srcFrame: { x: number; y: number; w: number; h: number },
	destX: number,
	destY: number
): void {
	for (let row = 0; row < srcFrame.h; row++) {
		const destY2 = destY + row;
		if (destY2 < 0 || destY2 >= dest.height) continue;

		for (let column = 0; column < srcFrame.w; column++) {
			const destX2 = destX + column;
			if (destX2 < 0 || destX2 >= dest.width) continue;

			const srcOffset = ((srcFrame.y + row) * src.width + srcFrame.x + column) * 4;
			const alpha = src.data[srcOffset + 3];
			if (alpha === 0) continue;

			const destOffset = (destY2 * dest.width + destX2) * 4;
			dest.data[destOffset] = src.data[srcOffset];
			dest.data[destOffset + 1] = src.data[srcOffset + 1];
			dest.data[destOffset + 2] = src.data[srcOffset + 2];
			dest.data[destOffset + 3] = alpha;
		}
	}
}
