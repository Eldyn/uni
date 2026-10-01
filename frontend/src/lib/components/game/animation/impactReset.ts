import { storeCameraOffset } from "$stores/cameraOffset.svelte";

/** Puts the camera and the screen edge back to rest. Runs when an impact's
 *  beat ends, naturally or skipped, since a skip suppresses tween callbacks. */
export function resetImpactEffects(): void {
	storeCameraOffset.clearOffset();
}
