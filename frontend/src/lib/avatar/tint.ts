import { classifyPixel } from "./stripFormat";
import { createImage, type PixelImage } from "./pixels";
import type { RegionIndex } from "./types";

export type Rgb = [number, number, number];

/** Parse a "#rrggbb" colour into its three channels. */
export function hexToRgb(hex: string): Rgb {
	return [
		parseInt(hex.slice(1, 3), 16),
		parseInt(hex.slice(3, 5), 16),
		parseInt(hex.slice(5, 7), 16)
	];
}

/**
 * Return a new image where each mask region is replaced by its tint colour,
 * scaled by the region shade. `colours[i]` tints region i (0-2 from the
 * primary file, 3-5 from `extraMasks[0]`); a region with no colour falls back
 * to a neutral grey of its shade, because multiply only darkens. Grey and
 * clear pixels copy through; invalid pixels go clear.
 *
 * INFO: an extra mask file only adds regions. The primary pixel decides the
 * alpha, so an extra pixel over a clear or invalid primary pixel is ignored,
 * and a grey, clear or invalid extra pixel is ignored. A missing extra image
 * or one of another size is skipped.
 */
export function tintImage(
	src: PixelImage,
	colours: Rgb[],
	extraMasks: (PixelImage | undefined)[] = []
): PixelImage {
	const out = createImage(src.width, src.height);

	for (let offset = 0; offset < src.data.length; offset += 4) {
		const pixel = classifyPixel(
			src.data[offset],
			src.data[offset + 1],
			src.data[offset + 2],
			src.data[offset + 3]
		);

		if (pixel.kind === "invalid") continue; // fully clear: output is zeroed

		if (pixel.kind !== "clear") {
			const extra = extraRegion(src, extraMasks, offset);
			const region = extra ?? (pixel.kind === "region" ? pixel : undefined);
			if (region) {
				paintRegion(out, offset, colours[region.index], region.shade);
				continue;
			}
		}

		out.data[offset] = src.data[offset];
		out.data[offset + 1] = src.data[offset + 1];
		out.data[offset + 2] = src.data[offset + 2];
		out.data[offset + 3] = src.data[offset + 3];
	}

	return out;
}

/** The first tint region an extra mask file holds at this pixel, if any. */
function extraRegion(
	src: PixelImage,
	extraMasks: (PixelImage | undefined)[],
	offset: number
): { index: RegionIndex; shade: number } | undefined {
	for (let fileIndex = 0; fileIndex < extraMasks.length; fileIndex++) {
		const mask = extraMasks[fileIndex];
		if (!mask || mask.width !== src.width || mask.height !== src.height) continue;

		const pixel = classifyPixel(
			mask.data[offset],
			mask.data[offset + 1],
			mask.data[offset + 2],
			mask.data[offset + 3],
			fileIndex + 1
		);
		if (pixel.kind === "region") return pixel;
	}
	return undefined;
}

/** Write one opaque tinted pixel: the colour times the shade, or grey. */
function paintRegion(out: PixelImage, offset: number, colour: Rgb | undefined, shade: number) {
	if (colour) {
		out.data[offset] = Math.round(colour[0] * shade);
		out.data[offset + 1] = Math.round(colour[1] * shade);
		out.data[offset + 2] = Math.round(colour[2] * shade);
	} else {
		const grey = Math.round(shade * 255);
		out.data[offset] = grey;
		out.data[offset + 1] = grey;
		out.data[offset + 2] = grey;
	}
	out.data[offset + 3] = 255;
}
