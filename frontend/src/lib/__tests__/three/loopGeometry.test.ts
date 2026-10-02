import { describe, it, expect } from "vitest";
import {
	fitLoopRect,
	loopLength,
	loopRect,
	outlineParam,
	pathPoint,
	sheetBlockCount,
	type LoopRect
} from "$components/game/three/loopGeometry";
import { DESKTOP_MAT_SHEET, PHONE_MAT_SHEET, type MatSheet } from "$components/game/layout/playmat";
import {
	LOOP_CELL_BLOCKS,
	LOOP_CORNER_RADIUS_BLOCKS,
	LOOP_INSET_BLOCKS,
	LOOP_MAX_CORNER_RADIUS_BLOCKS,
	LOOP_MIN_CORNER_RADIUS_BLOCKS
} from "$components/game/animation/loopPlan";

const rect: LoopRect = { centerX: 0, centerY: 0, halfWidth: 20, halfHeight: 12, cornerRadius: 4 };
const length = loopLength(rect);
const quarterArc = (Math.PI * 4) / 2;

function onArc(centerX: number, centerY: number, angle: number): [number, number] {
	return [centerX + 4 * Math.cos(angle), centerY + 4 * Math.sin(angle)];
}

function modularGap(a: number, b: number): number {
	const gap = Math.abs(a - b) % length;
	return Math.min(gap, length - gap);
}

describe("sheetBlockCount", () => {
	it("is the sheet over 4 texels per art pixel", () => {
		expect(sheetBlockCount(DESKTOP_MAT_SHEET)).toEqual({ x: 480, y: 270 });
		const portraitSheet: MatSheet = { ...DESKTOP_MAT_SHEET, texelWidth: 1080, texelHeight: 1920 };
		expect(sheetBlockCount(portraitSheet)).toEqual({ x: 270, y: 480 });
	});
});

describe("loopRect", () => {
	it("insets the desktop felt box by the given blocks, y up", () => {
		const insetRect = loopRect(
			sheetBlockCount(DESKTOP_MAT_SHEET),
			DESKTOP_MAT_SHEET.feltUvRect,
			10,
			10
		);
		// felt: x 162..324, y 75..195 (blocks); inset 10 -> x 172..314, y 85..185
		expect(insetRect.centerX).toBeCloseTo(243);
		expect(insetRect.centerY).toBeCloseTo(135);
		expect(insetRect.halfWidth).toBeCloseTo(71);
		expect(insetRect.halfHeight).toBeCloseTo(50);
		expect(insetRect.cornerRadius).toBe(10);
	});
});

describe("loopLength", () => {
	it("is the four straights plus the four quarter arcs", () => {
		expect(length).toBeCloseTo(4 * (16 + 8) + 2 * Math.PI * 4);
	});
});

const fitOptions = {
	cellLength: LOOP_CELL_BLOCKS,
	preferredCornerRadius: LOOP_CORNER_RADIUS_BLOCKS,
	minCornerRadius: LOOP_MIN_CORNER_RADIUS_BLOCKS,
	maxCornerRadius: LOOP_MAX_CORNER_RADIUS_BLOCKS
};

describe("fitLoopRect", () => {
	const sheets: [string, MatSheet][] = [
		["desktop", DESKTOP_MAT_SHEET],
		["phone", PHONE_MAT_SHEET]
	];

	for (const [name, sheet] of sheets) {
		const nominal = loopRect(
			sheetBlockCount(sheet),
			sheet.feltUvRect,
			LOOP_INSET_BLOCKS,
			LOOP_CORNER_RADIUS_BLOCKS
		);
		const fitted = fitLoopRect(nominal, fitOptions);

		it(`tiles the ${name} outline in a whole number of cells`, () => {
			const cells = loopLength(fitted) / LOOP_CELL_BLOCKS;
			expect(cells).toBeGreaterThanOrEqual(1);
			expect(cells).toBeCloseTo(Math.round(cells), 6);
		});

		it(`keeps the ${name} corner radius in range and the rectangle close to nominal`, () => {
			expect(fitted.cornerRadius).toBeGreaterThanOrEqual(LOOP_MIN_CORNER_RADIUS_BLOCKS);
			expect(fitted.cornerRadius).toBeLessThanOrEqual(LOOP_MAX_CORNER_RADIUS_BLOCKS);
			expect(Math.abs(fitted.halfWidth - nominal.halfWidth)).toBeLessThanOrEqual(3);
			expect(Math.abs(fitted.halfHeight - nominal.halfHeight)).toBeLessThanOrEqual(3);
			expect(Math.abs(fitted.centerX - nominal.centerX)).toBeLessThanOrEqual(1);
			expect(Math.abs(fitted.centerY - nominal.centerY)).toBeLessThanOrEqual(1);
		});

		it(`puts every ${name} edge on a row or column of whole pixels`, () => {
			for (const edge of [
				fitted.centerX - fitted.halfWidth,
				fitted.centerX + fitted.halfWidth,
				fitted.centerY - fitted.halfHeight,
				fitted.centerY + fitted.halfHeight
			]) {
				expect(edge - Math.floor(edge)).toBeCloseTo(0.5, 9);
			}
		});
	}

	it("refuses a rectangle no radius in range can tile", () => {
		const tiny: LoopRect = {
			centerX: 0.5,
			centerY: 0.5,
			halfWidth: 3,
			halfHeight: 3,
			cornerRadius: 2
		};
		expect(() => fitLoopRect(tiny, fitOptions)).toThrow();
	});
});

describe("pathPoint", () => {
	it("starts at the bottom-edge midpoint heading right", () => {
		const start = pathPoint(0, rect);
		expect(start.x).toBeCloseTo(0);
		expect(start.y).toBeCloseTo(-12);
		expect([start.directionX, start.directionY]).toEqual([1, 0]);
	});

	it("heads up the right edge, left along the top and down the left edge", () => {
		const cases: [number, number, number, number, number][] = [
			[16 + quarterArc + 8, 20, 0, 0, 1],
			[length / 2, 0, 12, -1, 0],
			[(3 * length) / 4, -20, 0, 0, -1]
		];
		for (const [arcLength, x, y, directionX, directionY] of cases) {
			const point = pathPoint(arcLength, rect);
			expect(point.x).toBeCloseTo(x, 9);
			expect(point.y).toBeCloseTo(y, 9);
			expect(point.directionX).toBeCloseTo(directionX, 9);
			expect(point.directionY).toBeCloseTo(directionY, 9);
		}
	});

	it("wraps past the loop length", () => {
		const wrapped = pathPoint(length + 5, rect);
		const direct = pathPoint(5, rect);
		expect(wrapped.x).toBeCloseTo(direct.x);
		expect(wrapped.y).toBeCloseTo(direct.y);
	});

	it("always heads along a unit vector and stays on the outline it is the inverse of", () => {
		for (let step = 0; step < 400; step++) {
			const arcLength = (step / 400) * length + 0.013;
			const point = pathPoint(arcLength, rect);
			expect(Math.hypot(point.directionX, point.directionY)).toBeCloseTo(1, 9);
			const back = outlineParam(point.x, point.y, rect);
			expect(back.across).toBeCloseTo(0, 6);
			expect(modularGap(back.s, arcLength)).toBeLessThan(1e-6);
		}
	});

	it("is continuous: a tiny step in s moves the point by about that much", () => {
		const epsilon = 0.01;
		for (let step = 0; step < 400; step++) {
			const arcLength = (step / 400) * length;
			const here = pathPoint(arcLength, rect);
			const next = pathPoint(arcLength + epsilon, rect);
			expect(Math.hypot(next.x - here.x, next.y - here.y)).toBeLessThan(epsilon * 1.01);
		}
	});
});

describe("outlineParam", () => {
	it("starts at the bottom-edge midpoint with s = 0, travelling toward +x", () => {
		expect(outlineParam(0, -12, rect).s).toBeCloseTo(0);
		expect(outlineParam(10, -12, rect).s).toBeCloseTo(10);
		expect(outlineParam(-10, -12, rect).s).toBeCloseTo(length - 10);
	});

	it("puts the right edge midpoint, top midpoint and left midpoint at 1/4+, 1/2 and 3/4 of the loop", () => {
		expect(outlineParam(20, 0, rect).s).toBeCloseTo(16 + quarterArc + 8);
		expect(outlineParam(0, 12, rect).s).toBeCloseTo(length / 2);
		expect(outlineParam(-20, 0, rect).s).toBeCloseTo((3 * length) / 4);
	});

	it("measures the middle of a corner arc along the arc", () => {
		const [x, y] = onArc(16, -8, -Math.PI / 4);
		expect(outlineParam(x, y, rect).s).toBeCloseTo(16 + quarterArc / 2);
	});

	it("increases counter-clockwise around the outline", () => {
		const outlinePointsCounterClockwise: [number, number][] = [
			[0, -12],
			onArc(16, -8, -Math.PI / 4),
			[20, 0],
			onArc(16, 8, Math.PI / 4),
			[0, 12],
			onArc(-16, 8, (3 * Math.PI) / 4),
			[-20, 0],
			onArc(-16, -8, (5 * Math.PI) / 4)
		];
		const parameters = outlinePointsCounterClockwise.map(([x, y]) => outlineParam(x, y, rect).s);
		for (let index = 1; index < parameters.length; index++) {
			expect(parameters[index]).toBeGreaterThan(parameters[index - 1]);
		}
	});

	it("reports zero across on the outline, positive outside, at most -radius deep inside", () => {
		expect(outlineParam(20, 0, rect).across).toBeCloseTo(0);
		expect(outlineParam(0, -14, rect).across).toBeCloseTo(2);
		expect(outlineParam(0, 0, rect).across).toBeLessThanOrEqual(-4);
	});

	// BUG-prone: the shader port must stay seamless where the outline changes piece.
	it("is continuous across every straight/arc junction and the s wrap", () => {
		const epsilon = 0.0025;
		const junctions: [[number, number], [number, number]][] = [
			[[15.99, -12], onArc(16, -8, -Math.PI / 2 + epsilon)],
			[onArc(16, -8, -epsilon), [20, -7.99]],
			[[20, 7.99], onArc(16, 8, epsilon)],
			[onArc(16, 8, Math.PI / 2 - epsilon), [15.99, 12]],
			[[-15.99, 12], onArc(-16, 8, Math.PI / 2 + epsilon)],
			[onArc(-16, 8, Math.PI - epsilon), [-20, 7.99]],
			[[-20, -7.99], onArc(-16, -8, Math.PI + epsilon)],
			[onArc(-16, -8, (3 * Math.PI) / 2 - epsilon), [-15.99, -12]],
			[
				[-0.01, -12],
				[0.01, -12]
			]
		];
		for (const [before, after] of junctions) {
			const gap = modularGap(
				outlineParam(before[0], before[1], rect).s,
				outlineParam(after[0], after[1], rect).s
			);
			expect(gap).toBeLessThan(0.05);
		}
	});
});
