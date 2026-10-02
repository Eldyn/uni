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

/** Pitch closest to `targetPitch` that divides `length` into whole cells. */
export function fitPitch(length: number, targetPitch: number): number {
	const cells = Math.max(1, Math.round(length / targetPitch));
	return length / cells;
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
