import { FRAME_SIZE } from "./types";

/** Sprite sizes that draw the 96 px frame at a whole pixel scale. */
export const FIT_SIZES = [384, 288, 192, 96] as const;
export type FitSize = (typeof FIT_SIZES)[number];

/** The whole scale of the 96 px frame that fits a box; 1 at minimum. */
export function wholePixelScale(boxPx: number): number {
	return Math.max(1, Math.floor(boxPx / FRAME_SIZE));
}

/** The largest whole-pixel sprite size that fits a box; 96 at minimum. */
export function fitSpriteSize(boxPx: number): FitSize {
	return FIT_SIZES.find((size) => size <= boxPx) ?? 96;
}
