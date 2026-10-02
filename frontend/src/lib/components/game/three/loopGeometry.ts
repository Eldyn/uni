/**
 * @file loopGeometry.ts
 * @brief The play-direction loop's rounded rectangle in the felt's art-pixel
 * block space (x right, y up) and a CPU reference of the maths the loop
 * fragment shader runs, so the shader's port is checkable by unit tests.
 *
 * The outline is traversed counter-clockwise on screen, starting at the
 * bottom edge's midpoint heading right. `s` is arc length along it, `across`
 * the signed distance from it (positive outside).
 */

import type { MatSheet, UvRect } from "../layout/playmat";
import { FELT_TEXELS_PER_ART_PIXEL } from "./ripple/ripplePlan";

/** The sheet's art-pixel grid: the unit the felt ripple and the loop work in. */
export function sheetBlockCount(sheet: MatSheet): { x: number; y: number } {
	return {
		x: sheet.texelWidth / FELT_TEXELS_PER_ART_PIXEL,
		y: sheet.texelHeight / FELT_TEXELS_PER_ART_PIXEL
	};
}

export interface LoopRect {
	centerX: number;
	centerY: number;
	halfWidth: number;
	halfHeight: number;
	cornerRadius: number;
}

export interface OutlinePoint {
	s: number;
	across: number;
}

export function loopRect(
	blockCount: { x: number; y: number },
	feltUv: UvRect,
	insetBlocks: number,
	cornerRadiusBlocks: number
): LoopRect {
	const left = feltUv.left * blockCount.x + insetBlocks;
	const right = feltUv.right * blockCount.x - insetBlocks;
	const bottom = feltUv.bottom * blockCount.y + insetBlocks;
	const top = feltUv.top * blockCount.y - insetBlocks;
	return {
		centerX: (left + right) / 2,
		centerY: (bottom + top) / 2,
		halfWidth: (right - left) / 2,
		halfHeight: (top - bottom) / 2,
		cornerRadius: cornerRadiusBlocks
	};
}

export function loopLength(rect: LoopRect): number {
	const straightHalfWidth = rect.halfWidth - rect.cornerRadius;
	const straightHalfHeight = rect.halfHeight - rect.cornerRadius;
	return 4 * (straightHalfWidth + straightHalfHeight) + 2 * Math.PI * rect.cornerRadius;
}

export interface LoopFitOptions {
	/** Length every chevron cell must tile the loop in, in blocks. */
	cellLength: number;
	preferredCornerRadius: number;
	minCornerRadius: number;
	maxCornerRadius: number;
}

/** Cost of each block an edge moves, weighed against a block of corner radius. */
const EDGE_SHIFT_COST = 1.5;
const MAX_EDGE_SHIFT_BLOCKS = 2;
const CELL_COUNT_SPAN = 1;

/**
 * Snaps the nominal rectangle to the pixel grid and nudges it so the outline's
 * length is a whole number of cells, which is what lets chevrons and dashes sit
 * an exact whole number of pixels apart all the way round.
 *
 * Edges land on block centres (an edge is then exactly one row or column of
 * pixels). The length is fixed up by moving the edges a block or two at most
 * and, continuously, by the corner radius, which has a wide enough range to
 * absorb whatever the whole-block edge steps leave over.
 */
export function fitLoopRect(nominal: LoopRect, options: LoopFitOptions): LoopRect {
	const snappedHalfWidth = Math.max(1, Math.round(nominal.halfWidth));
	const snappedHalfHeight = Math.max(1, Math.round(nominal.halfHeight));
	const centerX = Math.floor(nominal.centerX) + 0.5;
	const centerY = Math.floor(nominal.centerY) + 0.5;
	const cornerSlope = 8 - 2 * Math.PI;
	const nominalCells = loopLength(nominal) / options.cellLength;

	let best: { rect: LoopRect; cost: number } | null = null;
	for (let widthShift = -MAX_EDGE_SHIFT_BLOCKS; widthShift <= MAX_EDGE_SHIFT_BLOCKS; widthShift++) {
		for (
			let heightShift = -MAX_EDGE_SHIFT_BLOCKS;
			heightShift <= MAX_EDGE_SHIFT_BLOCKS;
			heightShift++
		) {
			const halfWidth = snappedHalfWidth + widthShift;
			const halfHeight = snappedHalfHeight + heightShift;
			for (
				let cells = Math.round(nominalCells) - CELL_COUNT_SPAN;
				cells <= Math.round(nominalCells) + CELL_COUNT_SPAN;
				cells++
			) {
				if (cells < 1) continue;
				const cornerRadius =
					(4 * (halfWidth + halfHeight) - cells * options.cellLength) / cornerSlope;
				if (cornerRadius < options.minCornerRadius || cornerRadius > options.maxCornerRadius) {
					continue;
				}
				if (cornerRadius * 2 > Math.min(halfWidth, halfHeight)) continue;
				const cost =
					(Math.abs(widthShift) + Math.abs(heightShift)) * EDGE_SHIFT_COST +
					Math.abs(cornerRadius - options.preferredCornerRadius);
				if (!best || cost < best.cost) {
					best = { rect: { centerX, centerY, halfWidth, halfHeight, cornerRadius }, cost };
				}
			}
		}
	}
	if (!best) throw new Error("no loop rectangle tiles the cell length within the radius range");
	return best.rect;
}

export interface PathPoint {
	x: number;
	y: number;
	/** Unit direction of travel (counter-clockwise on screen). */
	directionX: number;
	directionY: number;
}

/** Point and heading at arc length `s` along the outline, in block space. The
 *  GLSL `pathPoint` in turnLoop.frag.glsl mirrors this. */
export function pathPoint(arcLength: number, rect: LoopRect): PathPoint {
	const radius = rect.cornerRadius;
	const straightHalfWidth = rect.halfWidth - radius;
	const straightHalfHeight = rect.halfHeight - radius;
	const length = loopLength(rect);
	const s = ((arcLength % length) + length) % length;
	const quarterArc = (Math.PI * radius) / 2;
	const rightEdgeStart = straightHalfWidth + quarterArc;
	const topRightArcStart = rightEdgeStart + 2 * straightHalfHeight;
	const topEdgeStart = topRightArcStart + quarterArc;
	const topLeftArcStart = topEdgeStart + 2 * straightHalfWidth;
	const leftEdgeStart = topLeftArcStart + quarterArc;
	const bottomLeftArcStart = leftEdgeStart + 2 * straightHalfHeight;
	const bottomLeftStraightStart = bottomLeftArcStart + quarterArc;

	function onArc(arcCenterX: number, arcCenterY: number, startAngle: number, along: number) {
		const angle = startAngle + along / radius;
		return {
			x: arcCenterX + radius * Math.cos(angle),
			y: arcCenterY + radius * Math.sin(angle),
			directionX: -Math.sin(angle),
			directionY: Math.cos(angle)
		};
	}

	const { centerX, centerY, halfWidth, halfHeight } = rect;
	if (s < straightHalfWidth) {
		return { x: centerX + s, y: centerY - halfHeight, directionX: 1, directionY: 0 };
	}
	if (s < rightEdgeStart) {
		return onArc(
			centerX + straightHalfWidth,
			centerY - straightHalfHeight,
			-Math.PI / 2,
			s - straightHalfWidth
		);
	}
	if (s < topRightArcStart) {
		return {
			x: centerX + halfWidth,
			y: centerY - straightHalfHeight + (s - rightEdgeStart),
			directionX: 0,
			directionY: 1
		};
	}
	if (s < topEdgeStart) {
		return onArc(
			centerX + straightHalfWidth,
			centerY + straightHalfHeight,
			0,
			s - topRightArcStart
		);
	}
	if (s < topLeftArcStart) {
		return {
			x: centerX + straightHalfWidth - (s - topEdgeStart),
			y: centerY + halfHeight,
			directionX: -1,
			directionY: 0
		};
	}
	if (s < leftEdgeStart) {
		return onArc(
			centerX - straightHalfWidth,
			centerY + straightHalfHeight,
			Math.PI / 2,
			s - topLeftArcStart
		);
	}
	if (s < bottomLeftArcStart) {
		return {
			x: centerX - halfWidth,
			y: centerY + straightHalfHeight - (s - leftEdgeStart),
			directionX: 0,
			directionY: -1
		};
	}
	if (s < bottomLeftStraightStart) {
		return onArc(
			centerX - straightHalfWidth,
			centerY - straightHalfHeight,
			Math.PI,
			s - bottomLeftArcStart
		);
	}
	return {
		x: centerX - straightHalfWidth + (s - bottomLeftStraightStart),
		y: centerY - halfHeight,
		directionX: 1,
		directionY: 0
	};
}

/** Arc length and signed distance of a point relative to the outline. The
 *  GLSL `outlineParam` in turnLoop.frag.glsl mirrors this line for line. */
export function outlineParam(pointX: number, pointY: number, rect: LoopRect): OutlinePoint {
	const radius = rect.cornerRadius;
	const straightHalfWidth = rect.halfWidth - radius;
	const straightHalfHeight = rect.halfHeight - radius;
	const length = loopLength(rect);
	const offsetX = pointX - rect.centerX;
	const offsetY = pointY - rect.centerY;
	const absX = Math.abs(offsetX);
	const absY = Math.abs(offsetY);

	const outsideX = Math.max(absX - straightHalfWidth, 0);
	const outsideY = Math.max(absY - straightHalfHeight, 0);
	const across =
		Math.hypot(outsideX, outsideY) +
		Math.min(Math.max(absX - straightHalfWidth, absY - straightHalfHeight), 0) -
		radius;

	const quarterArc = (Math.PI * radius) / 2;
	const rightEdgeStart = straightHalfWidth + quarterArc;
	const topRightArcStart = rightEdgeStart + 2 * straightHalfHeight;
	const topEdgeStart = topRightArcStart + quarterArc;
	const topLeftArcStart = topEdgeStart + 2 * straightHalfWidth;
	const leftEdgeStart = topLeftArcStart + quarterArc;
	const bottomLeftArcStart = leftEdgeStart + 2 * straightHalfHeight;

	let arcLength: number;
	if (offsetX > straightHalfWidth && offsetY < -straightHalfHeight) {
		const angle = Math.atan2(offsetY + straightHalfHeight, offsetX - straightHalfWidth);
		arcLength = straightHalfWidth + radius * (angle + Math.PI / 2);
	} else if (offsetX > straightHalfWidth && offsetY > straightHalfHeight) {
		const angle = Math.atan2(offsetY - straightHalfHeight, offsetX - straightHalfWidth);
		arcLength = topRightArcStart + radius * angle;
	} else if (offsetX < -straightHalfWidth && offsetY > straightHalfHeight) {
		const angle = Math.atan2(offsetY - straightHalfHeight, offsetX + straightHalfWidth);
		arcLength = topLeftArcStart + radius * (angle - Math.PI / 2);
	} else if (offsetX < -straightHalfWidth && offsetY < -straightHalfHeight) {
		const angle = Math.atan2(offsetY + straightHalfHeight, offsetX + straightHalfWidth);
		arcLength = bottomLeftArcStart + radius * (angle + Math.PI);
	} else if (absX - straightHalfWidth >= absY - straightHalfHeight) {
		arcLength =
			offsetX > 0
				? rightEdgeStart + offsetY + straightHalfHeight
				: leftEdgeStart + straightHalfHeight - offsetY;
	} else if (offsetY > 0) {
		arcLength = topEdgeStart + straightHalfWidth - offsetX;
	} else {
		arcLength = offsetX >= 0 ? offsetX : length + offsetX;
	}
	return { s: arcLength, across };
}
