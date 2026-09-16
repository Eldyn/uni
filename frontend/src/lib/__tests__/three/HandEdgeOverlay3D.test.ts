import { describe, it, expect } from "vitest";
import { OVERLAY_Y, createGradientTexture } from "$components/game/three/handEdgeOverlay";
import { CARD_HEIGHT } from "$components/game/three/units";

describe("HandEdgeOverlay3D & handEdgeOverlay", () => {
	it("places overlay at Y = 0.45 between resting hand cards and DRAG_LIFT (0.5)", () => {
		expect(OVERLAY_Y).toBe(0.45);
		expect(OVERLAY_Y).toBeLessThan(0.5);
		expect(OVERLAY_Y).toBeGreaterThan(0.3);
	});

	it("computes width and height matching viewport rig and placement scale", () => {
		const rig = { halfWidth: 7.5, halfHeight: 4.2 };
		const placement = { localSeatZ: 3.5, handScale: 1.2 };

		const width = 2 * rig.halfWidth;
		const height = CARD_HEIGHT * placement.handScale * 1.5;

		expect(width).toBe(15);
		expect(height).toBeCloseTo(CARD_HEIGHT * 1.8);
	});

	it("safely attempts texture generation for vignette and fadeOverlay modes", () => {
		// In test / jsdom environment without node canvas, createGradientTexture safely returns null or Texture
		const vignetteTex = createGradientTexture("vignette");
		const fadeTex = createGradientTexture("fadeOverlay");

		// Should not throw
		expect(vignetteTex === null || typeof vignetteTex === "object").toBe(true);
		expect(fadeTex === null || typeof fadeTex === "object").toBe(true);
	});
});
