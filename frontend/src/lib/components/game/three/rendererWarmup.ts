/**
 * @file rendererWarmup.ts
 * @brief Pays the board's first-frame GPU warm-up while the loading screen is
 * still up.
 *
 * The first render of the board compiles every shader program and uploads the
 * baked card-face atlas. Measured at ~100-200ms, that cost otherwise lands on
 * the deal cinematic's opening frames. `Scene3D` awaits this before it tells the
 * server the client is ready, so the ready barrier holds the loader open until
 * the renderer is warm and the opening frames stay cheap.
 *
 * Bounded and non-throwing by design: a stalled or failed warm-up must never
 * keep the match from starting.
 */

import type { Camera, Scene, WebGLRenderer } from "three";
import { atlasPageCount, getAtlasPage } from "./cardFaceAtlas";

/** Upper bound on the warm-up. Whatever has not finished by then keeps
 *  compiling in the background; the match is allowed to start regardless. */
const WARM_TIMEOUT_MS = 2000;

/**
 * Uploads the atlas pages and pre-compiles the scene's shader programs.
 * Resolves once warm (or once the timeout elapses), never rejects.
 */
export async function warmRenderer(
	renderer: WebGLRenderer | undefined,
	scene: Scene | undefined,
	camera: Camera | undefined
): Promise<void> {
	if (!renderer) return;

	// Upload the baked atlas ahead of the first card draw.
	try {
		for (let page = 0; page < atlasPageCount(); page++) {
			renderer.initTexture(getAtlasPage(page));
		}
	} catch (err) {
		console.warn("rendererWarmup: atlas upload failed", err);
	}

	if (!scene || !camera) return;

	try {
		const compiled =
			typeof renderer.compileAsync === "function"
				? renderer.compileAsync(scene, camera)
				: Promise.resolve(renderer.compile(scene, camera));
		await Promise.race([
			compiled,
			new Promise<void>((resolve) => setTimeout(resolve, WARM_TIMEOUT_MS))
		]);
	} catch (err) {
		console.warn("rendererWarmup: shader pre-compile failed", err);
	}
}
