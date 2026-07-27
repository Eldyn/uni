/**
 * @file textures.ts
 * @brief Module-level texture cache shared by every CardMesh3D instance, so
 * loading N card backs or M copies of the same value image only ever hits
 * the network/disk once per URL, however many meshes reference it.
 */

import { TextureLoader, CanvasTexture, NearestFilter, SRGBColorSpace, type Texture } from "three";

const loader = new TextureLoader();
const cache = new Map<string, Promise<Texture>>();
const silhouetteCache = new Map<string, Promise<Texture>>();

/** Loads (or returns the cached) texture at `url`, pixelated to match the card art's `image-rendering: pixelated` DOM styling. */
export function loadTexture(url: string): Promise<Texture> {
	let promise = cache.get(url);
	if (!promise) {
		promise = new Promise<Texture>((resolve, reject) => {
			loader.load(
				url,
				(texture) => {
					texture.colorSpace = SRGBColorSpace;
					texture.magFilter = NearestFilter;
					texture.minFilter = NearestFilter;
					resolve(texture);
				},
				undefined,
				reject
			);
		});
		cache.set(url, promise);
	}
	return promise;
}

/**
 * Loads `url` and returns a copy with every opaque pixel forced to white,
 * keeping only its alpha — the texture's *shape*, with none of its color.
 *
 * A tinted `map` multiplies, so a shape whose art is dark (background.png is a
 * near-black #10141f rounded rect) drags any tint down to black no matter what
 * color you ask for. Bleaching the RGB first makes `color` mean what it says.
 * `alphaMap` is not the answer: three.js samples its green channel, which for
 * that same dark art is ~8% opacity, i.e. an invisible halo.
 */
export function loadSilhouette(url: string): Promise<Texture> {
	let promise = silhouetteCache.get(url);
	if (!promise) {
		promise = loadTexture(url).then((source) => {
			const image = source.image as CanvasImageSource & { width: number; height: number };
			const canvas = document.createElement("canvas");
			canvas.width = image.width;
			canvas.height = image.height;

			const ctx = canvas.getContext("2d")!;
			ctx.drawImage(image, 0, 0);
			// Paints white through the alpha already on the canvas, leaving the
			// transparent rounded corners transparent.
			ctx.globalCompositeOperation = "source-in";
			ctx.fillStyle = "#ffffff";
			ctx.fillRect(0, 0, canvas.width, canvas.height);

			const texture = new CanvasTexture(canvas);
			texture.colorSpace = SRGBColorSpace;
			texture.magFilter = NearestFilter;
			texture.minFilter = NearestFilter;
			return texture;
		});
		silhouetteCache.set(url, promise);
	}
	return promise;
}
