/**
 * @file cameraOffset.svelte.ts
 * @brief Transient offset on the board camera (the landing impact's punch).
 * Scene3D adds it to the camera rig's base position; the rig itself stays a
 * pure, viewport-derived value.
 */

import { ZERO_CAMERA_OFFSET, type CameraOffset } from "$components/game/layout/cameraRig";

class StoreCameraOffset {
	offset = $state.raw<Readonly<CameraOffset>>(ZERO_CAMERA_OFFSET);

	applyOffset(offset: CameraOffset): void {
		this.offset = { x: offset.x, y: offset.y, z: offset.z };
	}

	clearOffset(): void {
		this.offset = ZERO_CAMERA_OFFSET;
	}
}

export const storeCameraOffset = new StoreCameraOffset();
