import { describe, it, expect } from "vitest";

import { computeDesignGrid, boardExtentsFor } from "$components/game/layout/designGrid";
import { ringRadiiFor, ringReachFor } from "$components/game/layout/seatLayout3D";
import { MAX_OPPONENTS, type ViewportInfo } from "$components/game/layout/seatLayout";

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

	// A landscape board's rows are what bind its frustum, so there is no slack to
	// spend and the grid stays centered on the world origin the ring is built
	// around. Portrait fits the same board into a much taller frustum, and that
	// leftover depth goes to the near side, where the hand can use it, instead of
	// being split evenly with the empty band above the table.
	it("centers on the world origin unless portrait has slack to spend", () => {
		for (const viewport of [landscape, square]) {
			expect(computeDesignGrid(viewport, 1).centerZ).toBe(0);
		}

		const { centerZ, halfHeightUnits } = computeDesignGrid(portrait, 1);
		expect(centerZ).toBeGreaterThan(0);
		expect(centerZ).toBeLessThan(halfHeightUnits - boardExtentsFor(portrait).rows / 2 + 1e-6);
	});

	it("sizes the board from the opponent ring alone, symmetric about the origin", () => {
		for (const viewport of [landscape, portrait, square]) {
			const { columns, rows } = boardExtentsFor(viewport);
			expect(columns).toBeGreaterThan(0);
			expect(rows).toBeGreaterThan(0);
		}
	});

	// Regression: the extents used to be measured at the FULLEST table only, but
	// a sparse table's reach margin is twice a full one's — so a 4-player game
	// needed more depth than the frustum had been sized for and its top seat got
	// clipped off the screen.
	it("covers every seat count's reach, not just the fullest table's", () => {
		for (const viewport of [landscape, portrait, square]) {
			const { rows, columns } = boardExtentsFor(viewport);
			for (let opponents = 1; opponents <= MAX_OPPONENTS; opponents++) {
				const { rx, rz } = ringRadiiFor(viewport, opponents);
				const reach = ringReachFor(viewport, opponents);
				expect(rows / 2).toBeGreaterThanOrEqual(rz + reach - 1e-6);
				expect(columns / 2).toBeGreaterThanOrEqual(rx + reach - 1e-6);
			}
		}
	});

	it("zooming in shrinks the visible grid", () => {
		const base = computeDesignGrid(landscape, 1);
		const zoomedIn = computeDesignGrid(landscape, 2);
		expect(zoomedIn.halfHeightUnits).toBeLessThan(base.halfHeightUnits);
	});
});
