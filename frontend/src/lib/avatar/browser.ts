import type { PixelImage } from "./pixels";

/**
 * Fetch a PNG and decode it into a plain RGBA pixel buffer. INFO: this is
 * the only browser-specific edge of the bake path, kept separate so every
 * piece of bake logic stays testable without a canvas.
 */
export async function decodePng(url: string): Promise<PixelImage> {
	const response = await fetch(url, { credentials: "omit" });
	if (!response.ok) throw new Error(`avatar asset fetch failed: ${url}`);

	const bitmap = await createImageBitmap(await response.blob());
	try {
		const canvas = document.createElement("canvas");
		canvas.width = bitmap.width;
		canvas.height = bitmap.height;

		const context = canvas.getContext("2d");
		if (!context) throw new Error("avatar 2d context unavailable");

		context.drawImage(bitmap, 0, 0);
		const pixels = context.getImageData(0, 0, bitmap.width, bitmap.height);
		return { width: bitmap.width, height: bitmap.height, data: pixels.data };
	} finally {
		bitmap.close();
	}
}

/** Paint a baked pixel buffer onto a fresh canvas for display. */
export function toCanvas(img: PixelImage): HTMLCanvasElement {
	const canvas = document.createElement("canvas");
	canvas.width = img.width;
	canvas.height = img.height;

	const context = canvas.getContext("2d");
	context?.putImageData(
		new ImageData(new Uint8ClampedArray(img.data), img.width, img.height),
		0,
		0
	);

	return canvas;
}
