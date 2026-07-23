import { describe, it, expect } from "vitest";

import { computeDesignGrid, GRID_COLUMNS, GRID_ROWS } from "$components/game/layout/designGrid";
import type { ViewportInfo } from "$components/game/layout/seatLayout";

const landscape: ViewportInfo = { width: 1200, height: 800, orientation: "landscape" };
const portrait: ViewportInfo = { width: 390, height: 844, orientation: "portrait" };
const square: ViewportInfo = { width: 800, height: 800, orientation: "landscape" };

describe("computeDesignGrid", () => {
	it("matches the viewport's exact aspect ratio (no distortion, no letterboxing)", () => {
		for (const viewport of [landscape, portrait, square]) {
			const grid = computeDesignGrid(viewport, 1);
			expect(grid.halfWidthUnits / grid.halfHeightUnits).toBeCloseTo(viewport.width / viewport.height, 5);
		}
	});

	it("never shows fewer than GRID_COLUMNS x GRID_ROWS of the board", () => {
		for (const viewport of [landscape, portrait, square]) {
			const grid = computeDesignGrid(viewport, 1);
			expect(grid.halfWidthUnits * 2).toBeGreaterThanOrEqual(GRID_COLUMNS - 1e-6);
			expect(grid.halfHeightUnits * 2).toBeGreaterThanOrEqual(GRID_ROWS - 1e-6);
		}
	});

	it("zooming in shrinks the visible grid", () => {
		const base = computeDesignGrid(landscape, 1);
		const zoomedIn = computeDesignGrid(landscape, 2);
		expect(zoomedIn.halfHeightUnits).toBeLessThan(base.halfHeightUnits);
	});
});
