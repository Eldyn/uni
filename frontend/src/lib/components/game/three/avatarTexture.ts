/**
 * @file avatarTexture.ts
 * @brief Baked-avatar textures for the 3D seats. Each seat builds its own
 * CanvasTexture so its UV frame offset steps independently, while the pixels
 * come from the shared bake cache (one bake per loadout hash).
 */

import { CanvasTexture, NearestFilter, SRGBColorSpace } from "three";
import { getBakeCache } from "$lib/avatar/bakeCache";
import { decodePng, toCanvas } from "$lib/avatar/browser";
import { defaultLoadout } from "$lib/avatar/defaultLoadout";
import type { PixelImage } from "$lib/avatar/pixels";
import { FRAME_COUNT, type Loadout } from "$lib/avatar/types";

/** The gray robot strip; bots are never customised. */
export const BOT_STRIP_PATH = "/assets/bot_animated_strip.png";

/**
 * Wrap a baked strip in a pixelated CanvasTexture. INFO: `repeat.x` spans
 * FRAME_COUNT cells, so `offset.x = frame / FRAME_COUNT` shows one frame.
 */
export function createAvatarTexture(img: PixelImage): CanvasTexture {
	const texture = new CanvasTexture(toCanvas(img));
	texture.colorSpace = SRGBColorSpace;
	texture.magFilter = NearestFilter;
	texture.minFilter = NearestFilter;
	texture.repeat.set(1 / FRAME_COUNT, 1);
	texture.offset.x = 0;
	texture.needsUpdate = true;
	return texture;
}

/**
 * The strip texture for one seat. Bots load the shared robot sheet; every
 * other loadout is baked, and a failed bake falls back to the default look
 * so a seat never renders blank.
 */
export async function loadAvatarTexture(
	loadout: Loadout | null | undefined,
	isBot: boolean
): Promise<CanvasTexture> {
	if (isBot) return createAvatarTexture(await decodePng(BOT_STRIP_PATH));

	const cache = getBakeCache();
	try {
		const strip = await cache.get(loadout ?? defaultLoadout());
		return createAvatarTexture(strip.image);
	} catch {
		// WARN: the bake failed; the default look is the always-present fallback.
		const fallback = await cache.get(defaultLoadout());
		return createAvatarTexture(fallback.image);
	}
}
