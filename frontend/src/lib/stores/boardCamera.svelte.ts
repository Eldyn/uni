/**
 * @file boardCamera.svelte.ts
 * @brief The live scene camera, published for DOM overlays that have to project
 * world positions to screen space.
 *
 * The board is viewed through a tilted perspective camera, so a world→screen
 * conversion needs the camera itself (see screenProjection.ts). The camera is
 * created by Scene3D inside the Threlte `<Canvas>`, while the DOM overlays that
 * need it (DrawStackIndicator) render outside it — so rather
 * than thread a reference through the component tree, Scene3D publishes the
 * camera here on creation.
 */

import type { PerspectiveCamera } from "three";

class StoreBoardCamera {
	camera = $state<PerspectiveCamera | null>(null);
}

export const storeBoardCamera = new StoreBoardCamera();
