import { FRAME_SIZE, REGIONS_PER_FILE, STRIP_WIDTH, type RegionIndex } from "./types";
import type { PixelImage } from "./pixels";

export type MaskPixel =
	| { kind: "clear" }
	| { kind: "grey"; value: number }
	| { kind: "region"; index: RegionIndex; shade: number }
	| { kind: "invalid" };

export interface MaskProblem {
	x: number;
	y: number;
	reason: "partial-alpha" | "mixed-colour";
}

/**
 * Classify one mask pixel: clear, neutral grey, or a shaded tint region.
 * The lone non-zero R/G/B channel selects the region inside the file; the
 * region index is `3 * fileIndex + channel`, so file 0 gives regions 0-2 and
 * file 1 (the extra mask) gives 3-5. Mixed or partially transparent pixels
 * are invalid (masks are hard-edged).
 */
export function classifyPixel(
	r: number,
	g: number,
	b: number,
	a: number,
	fileIndex = 0
): MaskPixel {
	if (a === 0) return { kind: "clear" };
	if (a !== 255) return { kind: "invalid" };
	if (r === g && g === b) return { kind: "grey", value: r };

	const channels = [r, g, b];
	const lit = channels.filter((channel) => channel !== 0);
	if (lit.length !== 1) return { kind: "invalid" };

	const index = (fileIndex * REGIONS_PER_FILE + channels.indexOf(lit[0])) as RegionIndex;
	return { kind: "region", index, shade: lit[0] / 255 };
}

/** Whether the image is one 5-frame avatar strip (480x96). */
export function isStripSize(img: PixelImage): boolean {
	return img.width === STRIP_WIDTH && img.height === FRAME_SIZE;
}

/** Whether the image is one still avatar frame (96x96). */
export function isStillSize(img: PixelImage): boolean {
	return img.width === FRAME_SIZE && img.height === FRAME_SIZE;
}

/** The first invalid mask pixels, in row-major order, capped by `limit`. */
export function findMaskProblems(img: PixelImage, limit = 10): MaskProblem[] {
	const problems: MaskProblem[] = [];

	for (let y = 0; y < img.height && problems.length < limit; y++) {
		for (let x = 0; x < img.width && problems.length < limit; x++) {
			const offset = (y * img.width + x) * 4;
			const alpha = img.data[offset + 3];
			if (
				classifyPixel(img.data[offset], img.data[offset + 1], img.data[offset + 2], alpha).kind !==
				"invalid"
			) {
				continue;
			}
			problems.push({ x, y, reason: alpha !== 255 ? "partial-alpha" : "mixed-colour" });
		}
	}

	return problems;
}
