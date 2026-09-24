/**
 * @file skipMarkTexture.ts
 * @brief Canvas-drawn X sprite texture for the "player skipped" mark. Cached
 * once: every seat shares the same mark, and rebuilding the canvas per seat
 * would be wasted GPU uploads.
 */

import { CanvasTexture, NearestFilter, SRGBColorSpace } from "three";

const CANVAS_SIZE = 128;
const PAD = 30;
const STROKE = 18;
const OUTLINE = 10;
const MARK_COLOR = "#e0384a";
const OUTLINE_COLOR = "rgba(0, 0, 0, 0.85)";

let cached: CanvasTexture | null = null;

function strokeX(ctx: CanvasRenderingContext2D): void {
	const a = PAD;
	const b = CANVAS_SIZE - PAD;
	ctx.beginPath();
	ctx.moveTo(a, a);
	ctx.lineTo(b, b);
	ctx.moveTo(b, a);
	ctx.lineTo(a, b);
	ctx.stroke();
}

/** Returns the shared X texture, or null when there is no DOM (SSR/tests). */
export function skipMarkTexture(): CanvasTexture | null {
	if (cached) return cached;
	if (typeof document === "undefined") return null;

	const canvas = document.createElement("canvas");
	canvas.width = CANVAS_SIZE;
	canvas.height = CANVAS_SIZE;
	const ctx = canvas.getContext("2d");
	if (!ctx) return null;

	ctx.clearRect(0, 0, CANVAS_SIZE, CANVAS_SIZE);
	ctx.lineCap = "round";

	ctx.strokeStyle = OUTLINE_COLOR;
	ctx.lineWidth = STROKE + OUTLINE;
	strokeX(ctx);

	ctx.strokeStyle = MARK_COLOR;
	ctx.lineWidth = STROKE;
	strokeX(ctx);

	const texture = new CanvasTexture(canvas);
	texture.colorSpace = SRGBColorSpace;
	texture.magFilter = NearestFilter;
	texture.minFilter = NearestFilter;
	cached = texture;
	return cached;
}
