/**
 * @file assetTextures.ts
 * @brief Hash-keyed texture and emoji-glyph cache for mod faces.
 *
 * Mod images keep their own texture (arbitrary art need not be atlas-packed),
 * keyed by content hash so a changed asset is a new key. Emoji glyphs are
 * rasterised once per glyph into a `CanvasTexture`. `text` faces stay on the
 * procedural atlas in `cardFaceAtlas.ts`.
 */

import { CanvasTexture, LinearFilter, SRGBColorSpace, Texture, TextureLoader } from "three";
import { assetOrigin, isAllowedAssetUrl, verifyAssetBytes } from "./assetTrust";

/** @brief Loader seam so unit tests need no WebGL/canvas. */
export interface TextureLoaderLike {
	load(url: string, hash?: string): Texture;
}

const GLYPH_PX = 128;

/**
 * @class TrustedImageLoader
 * @brief Loads mod art through the trust pipeline: origin allowlist, a
 * credential-free fetch, and a SHA-256 check against the declared hash before
 * the bytes are decoded from an object URL.
 *
 * `load` keeps the synchronous `TextureLoaderLike` contract by returning a
 * placeholder `Texture` immediately and filling in its decoded image once the
 * checks pass; a rejected asset stays blank rather than rendering foreign
 * bytes. The object URL is revoked on both decode outcomes.
 */
export class TrustedImageLoader implements TextureLoaderLike {
	#loader = new TextureLoader();

	load(url: string, hash?: string): Texture {
		const texture = new Texture();
		texture.colorSpace = SRGBColorSpace;
		void this.#loadVerified(url, hash)
			.then((loaded) => {
				texture.image = loaded.image;
				texture.needsUpdate = true;
			})
			.catch(() => {
				// Fail closed: leave the texture blank.
			});
		return texture;
	}

	async #loadVerified(url: string, hash: string | undefined): Promise<Texture> {
		if (!isAllowedAssetUrl(url, assetOrigin())) {
			throw new Error("asset URL origin not allowed");
		}
		const res = await fetch(url, { credentials: "omit" });
		if (!res.ok) throw new Error("asset fetch failed");
		const bytes = await res.arrayBuffer();
		if (hash && !(await verifyAssetBytes(bytes, hash))) {
			throw new Error("asset hash mismatch");
		}
		const blobUrl = URL.createObjectURL(
			new Blob([bytes], { type: res.headers.get("content-type") ?? "application/octet-stream" })
		);
		try {
			return await new Promise<Texture>((resolve, reject) => {
				this.#loader.load(blobUrl, resolve, undefined, reject);
			});
		} finally {
			URL.revokeObjectURL(blobUrl);
		}
	}
}

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
		loader: TextureLoaderLike = new TrustedImageLoader(),
		renderGlyph: (glyph: string) => Texture = renderGlyphTexture
	) {
		this.#loader = loader;
		this.#renderGlyph = renderGlyph;
	}

	/** @brief Image texture for a content hash; one texture per hash. */
	getImage(hash: string, url: string): Texture {
		const cached = this.#images.get(hash);
		if (cached) return cached;
		if (!isAllowedAssetUrl(url, assetOrigin())) {
			throw new Error("asset URL origin not allowed");
		}
		const texture = this.#loader.load(url, hash);
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
