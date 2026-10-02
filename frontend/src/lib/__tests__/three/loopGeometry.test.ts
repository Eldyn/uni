import { describe, it, expect } from "vitest";
import {
	fitPitch,
	loopLength,
	loopRect,
	outlineParam,
	sheetBlockCount,
	type LoopRect
} from "$components/game/three/loopGeometry";
import { DESKTOP_MAT_SHEET, type MatSheet } from "$components/game/layout/playmat";

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
		// felt: x 160..320, y 79..195 (blocks); inset 10 -> x 170..310, y 89..185
		expect(insetRect.centerX).toBeCloseTo(240);
		expect(insetRect.centerY).toBeCloseTo(137);
		expect(insetRect.halfWidth).toBeCloseTo(70);
		expect(insetRect.halfHeight).toBeCloseTo(48);
		expect(insetRect.cornerRadius).toBe(10);
	});
});

describe("loopLength", () => {
	it("is the four straights plus the four quarter arcs", () => {
		expect(length).toBeCloseTo(4 * (16 + 8) + 2 * Math.PI * 4);
	});
});

describe("fitPitch", () => {
	it("returns a pitch that tiles the loop in a whole number of cells near the target", () => {
		const pitch = fitPitch(length, 24);
		const cells = length / pitch;
		expect(cells).toBeCloseTo(Math.round(cells));
		expect(cells).toBeCloseTo(5);
		expect(Math.abs(pitch - 24)).toBeLessThan(24 / 2);
	});

	it("never yields zero cells for a tiny loop", () => {
		expect(fitPitch(3, 24)).toBeCloseTo(3);
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
