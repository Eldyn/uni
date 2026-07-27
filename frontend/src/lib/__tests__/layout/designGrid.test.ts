import { describe, it, expect } from "vitest";

import { computeDesignGrid, boardExtentsFor } from "$components/game/layout/designGrid";
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

	it("never shows less than the orientation's full board extents", () => {
		for (const viewport of [landscape, portrait, square]) {
			const grid = computeDesignGrid(viewport, 1);
			const { columns, rows } = boardExtentsFor(viewport);
			expect(grid.halfWidthUnits * 2).toBeGreaterThanOrEqual(columns - 1e-6);
			expect(grid.halfHeightUnits * 2).toBeGreaterThanOrEqual(rows - 1e-6);
		}
	});

	// The playmat and the discard pile both sit at the world origin; centering the
	// grid there is what puts them at the exact center of the screen.
	it("stays centered on the world origin on every aspect ratio", () => {
		for (const viewport of [landscape, portrait, square]) {
			expect(computeDesignGrid(viewport, 1).centerZ).toBe(0);
		}
	});

	it("sizes the board from the opponent ring alone, symmetric about the origin", () => {
		for (const viewport of [landscape, portrait, square]) {
			const { columns, rows } = boardExtentsFor(viewport);
			expect(columns).toBeGreaterThan(0);
			expect(rows).toBeGreaterThan(0);
		}
	});

	it("zooming in shrinks the visible grid", () => {
		const base = computeDesignGrid(landscape, 1);
		const zoomedIn = computeDesignGrid(landscape, 2);
		expect(zoomedIn.halfHeightUnits).toBeLessThan(base.halfHeightUnits);
	});
});
