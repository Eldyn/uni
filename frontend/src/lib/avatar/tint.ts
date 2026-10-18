import { classifyPixel } from "./stripFormat";
import { createImage, type PixelImage } from "./pixels";

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
 * scaled by the region shade. `colours[i]` tints region i; a region with no
 * colour falls back to a neutral grey of its shade, because multiply only
 * darkens. Grey and clear pixels copy through; invalid pixels go clear.
 */
export function tintImage(src: PixelImage, colours: Rgb[]): PixelImage {
	const out = createImage(src.width, src.height);

	for (let offset = 0; offset < src.data.length; offset += 4) {
		const pixel = classifyPixel(
			src.data[offset],
			src.data[offset + 1],
			src.data[offset + 2],
			src.data[offset + 3]
		);

		if (pixel.kind === "region") {
			const shade = pixel.shade;
			const colour = colours[pixel.index];
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
			continue;
		}

		if (pixel.kind === "invalid") continue; // fully clear: output is zeroed

		out.data[offset] = src.data[offset];
		out.data[offset + 1] = src.data[offset + 1];
		out.data[offset + 2] = src.data[offset + 2];
		out.data[offset + 3] = src.data[offset + 3];
	}

	return out;
}
