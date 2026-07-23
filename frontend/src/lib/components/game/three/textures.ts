/**
 * @file textures.ts
 * @brief Module-level texture cache shared by every CardMesh3D instance, so
 * loading N card backs or M copies of the same value image only ever hits
 * the network/disk once per URL, however many meshes reference it.
 */

import { TextureLoader, NearestFilter, SRGBColorSpace, type Texture } from "three";

const loader = new TextureLoader();
const cache = new Map<string, Promise<Texture>>();

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
