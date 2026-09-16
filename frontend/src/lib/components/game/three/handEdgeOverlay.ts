/**
 * @file handEdgeOverlay.ts
 * @brief Helper logic and texture generation for hand edge effect overlays (vignette & fadeOverlay).
 */
import { CanvasTexture, ClampToEdgeWrapping } from "three";

export const OVERLAY_Y = 0.45;

export function createGradientTexture(
	effectMode: "vignette" | "fadeOverlay"
): CanvasTexture | null {
	if (typeof document === "undefined") return null;
	try {
		const canvas = document.createElement("canvas");
		canvas.width = 256;
		canvas.height = 1;
		const ctx = canvas.getContext?.("2d");
		if (!ctx) return null;

		const grad = ctx.createLinearGradient(0, 0, 256, 0);
		if (effectMode === "vignette") {
			grad.addColorStop(0, "rgba(0, 0, 0, 0.85)");
			grad.addColorStop(0.18, "rgba(0, 0, 0, 0.35)");
			grad.addColorStop(0.35, "rgba(0, 0, 0, 0)");
			grad.addColorStop(0.65, "rgba(0, 0, 0, 0)");
			grad.addColorStop(0.82, "rgba(0, 0, 0, 0.35)");
			grad.addColorStop(1, "rgba(0, 0, 0, 0.85)");
		} else {
			grad.addColorStop(0, "rgba(16, 17, 20, 1.0)");
			grad.addColorStop(0.25, "rgba(16, 17, 20, 0.65)");
			grad.addColorStop(0.4, "rgba(16, 17, 20, 0)");
			grad.addColorStop(0.6, "rgba(16, 17, 20, 0)");
			grad.addColorStop(0.75, "rgba(16, 17, 20, 0.65)");
			grad.addColorStop(1, "rgba(16, 17, 20, 1.0)");
		}
		ctx.fillStyle = grad;
		ctx.fillRect(0, 0, 256, 1);

		const texture = new CanvasTexture(canvas);
		texture.wrapS = ClampToEdgeWrapping;
		texture.wrapT = ClampToEdgeWrapping;
		texture.needsUpdate = true;
		return texture;
	} catch {
		return null;
	}
}
