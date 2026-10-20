import { FRAME_SIZE } from "./types";

/** The whole scale of the 96 px frame that fits a box; 1 at minimum. */
export function wholePixelScale(boxPx: number): number {
	return Math.max(1, Math.floor(boxPx / FRAME_SIZE));
}

/** The largest integer sprite size that fits a box, capped at four whole frames. */
export function fitSpriteSize(boxPx: number): number {
	return Math.max(1, Math.min(384, Math.floor(boxPx)));
}
