import { describe, it, expect } from "vitest";
import { computeAnchorPlacement, type Rect, type Size } from "$lib/utils/anchorPlacement";

describe("anchorPlacement helper", () => {
	const viewport: Size = { width: 1000, height: 800 };
	const contentSize: Size = { width: 200, height: 100 };

	it("places popover above trigger when side is 'top' and fits", () => {
		const trigger: Rect = { left: 400, top: 300, width: 100, height: 50 };
		const result = computeAnchorPlacement(trigger, contentSize, {
			side: "top",
			offset: 10,
			margin: 8,
			viewport
		});

		expect(result.side).toBe("top");
		// y = trigger.top (300) - content.height (100) - offset (10) = 190
		expect(result.y).toBe(190);
		// centered x = 400 + (100 - 200) / 2 = 350
		expect(result.x).toBe(350);
	});

	it("flips to bottom when top clips the viewport edge", () => {
		const trigger: Rect = { left: 400, top: 50, width: 100, height: 50 };
		const result = computeAnchorPlacement(trigger, contentSize, {
			side: "top",
			offset: 10,
			margin: 8,
			viewport
		});

		// 50 - 100 - 10 = -60 < margin (8) -> flips to bottom
		expect(result.side).toBe("bottom");
		// y = 50 + 50 + 10 = 110
		expect(result.y).toBe(110);
	});

	it("flips to top when bottom clips the viewport edge", () => {
		const trigger: Rect = { left: 400, top: 720, width: 100, height: 50 };
		const result = computeAnchorPlacement(trigger, contentSize, {
			side: "bottom",
			offset: 10,
			margin: 8,
			viewport
		});

		// 720 + 50 + 10 + 100 = 880 > 800 - 8 = 792 -> flips to top
		expect(result.side).toBe("top");
		// y = 720 - 100 - 10 = 610
		expect(result.y).toBe(610);
	});

	it("flips to left when right clips the viewport edge", () => {
		const trigger: Rect = { left: 850, top: 300, width: 100, height: 50 };
		const result = computeAnchorPlacement(trigger, contentSize, {
			side: "right",
			offset: 10,
			margin: 8,
			viewport
		});

		// 850 + 100 + 10 + 200 = 1160 > 1000 - 8 = 992 -> flips to left
		expect(result.side).toBe("left");
		// x = 850 - 200 - 10 = 640
		expect(result.x).toBe(640);
	});

	it("flips to right when left clips the viewport edge", () => {
		const trigger: Rect = { left: 50, top: 300, width: 100, height: 50 };
		const result = computeAnchorPlacement(trigger, contentSize, {
			side: "left",
			offset: 10,
			margin: 8,
			viewport
		});

		// 50 - 200 - 10 = -160 < margin (8) -> flips to right
		expect(result.side).toBe("right");
		// x = 50 + 100 + 10 = 160
		expect(result.x).toBe(160);
	});

	it("clamps cross-axis position within viewport boundaries", () => {
		// Trigger near right edge, top placement
		const trigger: Rect = { left: 900, top: 300, width: 80, height: 50 };
		const result = computeAnchorPlacement(trigger, contentSize, {
			side: "top",
			offset: 10,
			margin: 8,
			viewport
		});

		expect(result.side).toBe("top");
		// Unclamped centered x = 900 + (80 - 200) / 2 = 840.
		// 840 + 200 = 1040 > 1000 - 8 = 992.
		// Clamped to 1000 - 8 - 200 = 792.
		expect(result.x).toBe(792);
		expect(result.x + contentSize.width).toBeLessThanOrEqual(viewport.width - 8);
	});

	it("avoids specified avoidRects when provided", () => {
		const trigger: Rect = { left: 400, top: 300, width: 100, height: 50 };
		// Existing obstacle above trigger
		const obstacle: Rect = { left: 300, top: 150, width: 300, height: 150 };

		const result = computeAnchorPlacement(trigger, contentSize, {
			side: "top",
			offset: 10,
			margin: 8,
			viewport,
			avoidRects: [obstacle]
		});

		// Should not pick 'top' because it overlaps obstacle
		expect(result.side).not.toBe("top");
		expect(result.side).toBe("bottom");
	});
});
