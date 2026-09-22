/**
 * @file assetTextures.ts
 * @brief Hash-keyed texture and emoji-glyph cache for mod faces.
 *
 * Mod images keep their own texture (arbitrary art need not be atlas-packed),
 * keyed by content hash so a changed asset is a new key. Emoji glyphs are
 * rasterised once per glyph into a `CanvasTexture`. `text` faces stay on the
 * procedural atlas in `cardFaceAtlas.ts`.
 */

import { CanvasTexture, LinearFilter, SRGBColorSpace, TextureLoader, type Texture } from "three";

/** @brief Loader seam so unit tests need no WebGL/canvas. */
export interface TextureLoaderLike {
	load(url: string): Texture;
}

const GLYPH_PX = 128;

function renderGlyphTexture(glyph: string): Texture {
	const canvas = document.createElement("canvas");
	canvas.width = GLYPH_PX;
	canvas.height = GLYPH_PX;
	const ctx = canvas.getContext("2d");
	if (ctx) {
		ctx.clearRect(0, 0, GLYPH_PX, GLYPH_PX);
		ctx.font = `${Math.floor(GLYPH_PX * 0.7)}px sans-serif`;
		ctx.textAlign = "center";
		ctx.textBaseline = "middle";
		ctx.fillStyle = "#ffffff";
		ctx.fillText(glyph, GLYPH_PX / 2, GLYPH_PX / 2);
	}
	const texture = new CanvasTexture(canvas);
	texture.colorSpace = SRGBColorSpace;
	texture.minFilter = LinearFilter;
	texture.needsUpdate = true;
	return texture;
}

/**
 * @class AssetTextureCache
 * @brief Content-hash-keyed image textures plus a per-glyph canvas cache.
 */
export class AssetTextureCache {
	#images = new Map<string, Texture>();
	#glyphs = new Map<string, Texture>();
	#loader: TextureLoaderLike;
	#renderGlyph: (glyph: string) => Texture;

	constructor(
		loader: TextureLoaderLike = new TextureLoader(),
		renderGlyph: (glyph: string) => Texture = renderGlyphTexture
	) {
		this.#loader = loader;
		this.#renderGlyph = renderGlyph;
	}

	/** @brief Image texture for a content hash; one texture per hash. */
	getImage(hash: string, url: string): Texture {
		const cached = this.#images.get(hash);
		if (cached) return cached;
		const texture = this.#loader.load(url);
		texture.colorSpace = SRGBColorSpace;
		this.#images.set(hash, texture);
		return texture;
	}

	/** @brief Canvas texture for an emoji glyph; one per glyph. */
	getGlyph(glyph: string): Texture {
		const cached = this.#glyphs.get(glyph);
		if (cached) return cached;
		const texture = this.#renderGlyph(glyph);
		this.#glyphs.set(glyph, texture);
		return texture;
	}

	clear(): void {
		for (const texture of this.#images.values()) texture.dispose();
		for (const texture of this.#glyphs.values()) texture.dispose();
		this.#images.clear();
		this.#glyphs.clear();
	}

	get imageCount(): number {
		return this.#images.size;
	}

	get glyphCount(): number {
		return this.#glyphs.size;
	}
}

export const assetTextures = new AssetTextureCache();
