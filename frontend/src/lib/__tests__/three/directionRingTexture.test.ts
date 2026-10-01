import { describe, it, expect } from "vitest";
import { directionRingAlpha } from "$components/game/three/directionRingTexture";

const SIZE = 256;
const OPAQUE = 255;
const CHEVRON_COUNT = 12;

const alpha = directionRingAlpha(SIZE);
const inkAt = (x: number, y: number) => alpha[Math.round(y) * SIZE + Math.round(x)] === OPAQUE;

/** Inked texels in a small box, to compare two spots of the ring. */
function inkNear(x: number, y: number, radius: number): number {
	let count = 0;
	for (let dy = -radius; dy <= radius; dy++) {
		for (let dx = -radius; dx <= radius; dx++) if (inkAt(x + dx, y + dy)) count++;
	}
	return count;
}

describe("directionRingAlpha", () => {
	it("uses hard edges only, no partial or dithered alpha", () => {
		const levels = new Set(alpha);
		expect([...levels].sort()).toEqual([0, OPAQUE]);
	});

	it("leaves no dither specks that read as scratches", () => {
		const MIN_MARK_TEXELS = 4;
		const seen = new Uint8Array(alpha.length);
		const markSizes: number[] = [];
		for (let start = 0; start < alpha.length; start++) {
			if (alpha[start] !== OPAQUE || seen[start]) continue;
			seen[start] = 1;
			const stack = [start];
			let size = 0;
			while (stack.length > 0) {
				const texel = stack.pop() as number;
				size++;
				const x = texel % SIZE;
				const neighbours = [
					x > 0 ? texel - 1 : -1,
					x < SIZE - 1 ? texel + 1 : -1,
					texel - SIZE,
					texel + SIZE
				];
				for (const next of neighbours) {
					if (next < 0 || next >= alpha.length) continue;
					if (alpha[next] !== OPAQUE || seen[next]) continue;
					seen[next] = 1;
					stack.push(next);
				}
			}
			markSizes.push(size);
		}
		expect(Math.min(...markSizes)).toBeGreaterThanOrEqual(MIN_MARK_TEXELS);
	});

	it("draws the same mark at every chevron slot", () => {
		const center = SIZE / 2;
		const cellAngle = (Math.PI * 2) / CHEVRON_COUNT;
		const ringRadius = center * 0.9;
		const counts = Array.from({ length: CHEVRON_COUNT }, (_, slot) =>
			inkNear(
				center + Math.cos(slot * cellAngle) * ringRadius,
				center + Math.sin(slot * cellAngle) * ringRadius,
				8
			)
		);
		const spread = Math.max(...counts) - Math.min(...counts);
		expect(Math.min(...counts)).toBeGreaterThan(0);
		expect(spread / Math.max(...counts)).toBeLessThan(0.25);
	});

	it("points the top chevron counter-clockwise (apex to the left on screen)", () => {
		const center = SIZE / 2;
		const topY = center - center * 0.9;
		const armOffset = 6;
		// INFO: forward play runs counter-clockwise on screen (next seat is
		// due right), so at the top the arms spread at the tail (right) and
		// meet at the apex (left); off-centre rows carry ink on the right only.
		expect(inkNear(center + 4, topY - armOffset, 1)).toBeGreaterThan(0);
		expect(inkNear(center - 4, topY - armOffset, 1)).toBe(0);
	});
});
