/**
 * @file seatLabel.ts
 * @brief Pure sizing/placement math (plus one canvas rasterizer) for an
 * opponent seat's overhead name label.
 *
 * PlayerSeat3D used to render this label as an SVG arc inside an HTML overlay,
 * which is why it kept its CSS-pixel size on mobile while the world-space
 * avatar shrank. The label now lives in the scene as a canvas-texture sprite;
 * this module holds the orientation-agnostic font-fit rules (moved verbatim
 * from the component so they stay unit-testable) and the arc glyph placement,
 * leaving only texture plumbing in the Svelte component.
 */

import { AVATAR_SPRITE_FILL } from "../layout/boardPlacement";

/** Mirrors app.css's `--tiny` stack without its nested `var()` (canvas fonts
 *  can't resolve custom-property references). */
export const SEAT_LABEL_FONT = '"LanaPixel", "Habbo", monospace';

/** The original SVG arc swept from -172° to -8°: 164° across the top. */
export const SEAT_LABEL_ARC_START_DEG = -172;
export const SEAT_LABEL_ARC_SWEEP_DEG = 164;

/** Names shown on the arc are capped at 16 chars, ellipsized in the middle of
 *  the 16th, so an unwieldy username can't dominate the seat. */
export function formatSeatName(username: string | undefined | null): string {
	if (!username) return "";
	return username.length > 16 ? username.slice(0, 15) + "…" : username;
}

export function estimateTextUnits(text: string): number {
	let units = 0;
	for (const ch of text) {
		const code = ch.charCodeAt(0);
		if (code > 0x2e80) {
			units += 1.0;
		} else if (code <= 0x0020) {
			units += 0.3;
		} else if ("ijl|!:'.,;".includes(ch)) {
			units += 0.35;
		} else if ("mwMW@#%&".includes(ch)) {
			units += 0.85;
		} else {
			units += 0.62;
		}
	}
	units += text.length * 0.04;
	return Math.max(1, units);
}

export interface SeatLabelLayout {
	baseFontSize: number;
	approxPathLength: number;
	maxAllowedTextLength: number;
	fontSize: number;
	dy: number;
	needsCompression: boolean;
}

/**
 * Font size (px) and baseline shift for `displayName`, shrunk so it fits the
 * arc length at `overheadRadius`. `labelEm` is the world-scaled base em.
 */
export function computeSeatLabelLayout(
	displayName: string,
	overheadRadius: number,
	labelEm: number
): SeatLabelLayout {
	const baseFontSize = Math.round(labelEm * 18);
	const approxPathLength = overheadRadius * ((SEAT_LABEL_ARC_SWEEP_DEG * Math.PI) / 180);
	const maxAllowedTextLength = Math.floor(approxPathLength * 0.9);

	if (!displayName) {
		return {
			baseFontSize,
			approxPathLength,
			maxAllowedTextLength,
			fontSize: baseFontSize,
			dy: -Math.max(4, Math.round(baseFontSize * 0.43)),
			needsCompression: false
		};
	}

	const units = estimateTextUnits(displayName);
	const maxFit = Math.floor(maxAllowedTextLength / units);
	const fontSize = Math.max(9, Math.min(baseFontSize, maxFit));
	return {
		baseFontSize,
		approxPathLength,
		maxAllowedTextLength,
		fontSize,
		dy: -Math.max(4, Math.round(fontSize * 0.43)),
		needsCompression: units * fontSize > maxAllowedTextLength
	};
}

/**
 * CSS pixels → world units for a seat whose avatar FIGURE is `avatarWorld`
 * tall and whose avatar FRAME is `avatarPx` CSS px wide. Scene3D derives
 * `avatarPx` from `avatarWorld` via the same AVATAR_SPRITE_FILL ratio, so
 * converting the label canvas through this keeps it the same apparent size as
 * the avatar at every viewport/zoom (the mobile-oversizing fix).
 */
export function seatWorldPerPx(avatarWorld: number, avatarPx: number): number {
	return avatarWorld / AVATAR_SPRITE_FILL / Math.max(1, avatarPx);
}

export interface ArcGlyph {
	char: string;
	x: number;
	y: number;
	rotationRad: number;
}

/**
 * Places glyphs along a circle of `radius`, centred on the top of the arc
 * (angle -90°), each rotated to sit tangent. `maxTextLength` > 0 compresses
 * the advances (the `textLength`/`lengthAdjust="spacingAndGlyphs"` fallback)
 * instead of letting long names overrun the arc.
 */
export function layoutArcGlyphs(
	glyphs: { char: string; width: number }[],
	radius: number,
	maxTextLength: number
): ArcGlyph[] {
	if (glyphs.length === 0 || radius <= 0) return [];

	const totalWidth = glyphs.reduce((sum, g) => sum + g.width, 0);
	const scale = maxTextLength > 0 && totalWidth > maxTextLength ? maxTextLength / totalWidth : 1;
	const advances = glyphs.map((g) => g.width * scale);
	const totalAdvance = advances.reduce((sum, a) => sum + a, 0);
	const totalAngle = totalAdvance / radius;

	let cursor = -Math.PI / 2 - totalAngle / 2;
	const placed: ArcGlyph[] = [];
	for (let i = 0; i < glyphs.length; i++) {
		const advanceAngle = advances[i] / radius;
		const angle = cursor + advanceAngle / 2;
		placed.push({
			char: glyphs[i].char,
			x: Math.cos(angle) * radius,
			y: Math.sin(angle) * radius,
			rotationRad: angle + Math.PI / 2
		});
		cursor += advanceAngle;
	}
	return placed;
}

/**
 * Rasterizes the rail + arc text onto a fresh square canvas whose centre is
 * the arc's centre, or returns null where there's no 2D context (SSR/jsdom).
 */
export function drawSeatLabel(
	displayName: string,
	layout: SeatLabelLayout,
	overheadRadius: number,
	accent: string,
	active: boolean
): HTMLCanvasElement | null {
	if (typeof document === "undefined") return null;

	const padding = Math.ceil(layout.fontSize * 1.8);
	const halfSide = Math.ceil(overheadRadius + padding);
	const size = halfSide * 2;

	const canvas = document.createElement("canvas");
	canvas.width = size;
	canvas.height = size;
	const ctx = canvas.getContext("2d");
	if (!ctx) return null;

	ctx.translate(halfSide, halfSide);

	const startRad = (SEAT_LABEL_ARC_START_DEG * Math.PI) / 180;
	const endRad = ((SEAT_LABEL_ARC_START_DEG + SEAT_LABEL_ARC_SWEEP_DEG) * Math.PI) / 180;
	ctx.beginPath();
	ctx.arc(0, 0, overheadRadius, startRad, endRad);
	ctx.strokeStyle = active ? accent : "rgba(255, 255, 255, 0.28)";
	ctx.lineWidth = active ? 2 : 1.5;
	ctx.setLineDash(active ? [] : [4, 3]);
	ctx.lineCap = "round";
	ctx.stroke();

	if (!displayName) return canvas;

	ctx.font = `${active ? "bold " : ""}${layout.fontSize}px ${SEAT_LABEL_FONT}`;
	ctx.fillStyle = "#ffffff";
	ctx.textAlign = "center";
	ctx.textBaseline = "alphabetic";
	ctx.shadowColor = "rgba(0, 0, 0, 0.9)";
	ctx.shadowBlur = 2;
	ctx.shadowOffsetY = 1;

	const glyphs = [...displayName].map((char) => ({ char, width: ctx.measureText(char).width }));
	const placed = layoutArcGlyphs(
		glyphs,
		overheadRadius,
		layout.needsCompression ? layout.maxAllowedTextLength : 0
	);
	for (const glyph of placed) {
		ctx.save();
		ctx.translate(glyph.x, glyph.y);
		ctx.rotate(glyph.rotationRad);
		ctx.fillText(glyph.char, 0, 0);
		ctx.restore();
	}

	return canvas;
}
