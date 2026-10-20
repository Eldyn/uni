import { describe, it, expect } from "vitest";
import { blit, clipToAlpha, createImage, type PixelImage } from "$lib/avatar/pixels";

function pixelAt(img: PixelImage, x: number, y: number): number[] {
	const offset = (y * img.width + x) * 4;
	return Array.from(img.data.slice(offset, offset + 4));
}

function setPixel(img: PixelImage, x: number, y: number, rgba: number[]): void {
	const offset = (y * img.width + x) * 4;
	img.data.set(rgba, offset);
}

describe("pixels", () => {
	it("createImage zeroes the buffer", () => {
		const img = createImage(2, 3);

		expect(img.width).toBe(2);
		expect(img.height).toBe(3);
		expect(img.data.length).toBe(2 * 3 * 4);
		expect(img.data.every((value) => value === 0)).toBe(true);
	});

	it("blit copies an opaque pixel", () => {
		const src = createImage(2, 2);
		setPixel(src, 1, 0, [10, 20, 30, 255]);
		const dest = createImage(4, 4);

		blit(dest, src, { x: 1, y: 0, w: 1, h: 1 }, 3, 2);

		expect(pixelAt(dest, 3, 2)).toEqual([10, 20, 30, 255]);
		expect(pixelAt(dest, 2, 2)).toEqual([0, 0, 0, 0]);
		expect(pixelAt(dest, 3, 1)).toEqual([0, 0, 0, 0]);
	});

	it("blit leaves dest alone for a clear source pixel", () => {
		const src = createImage(1, 1);
		setPixel(src, 0, 0, [200, 100, 50, 0]);
		const dest = createImage(1, 1);
		setPixel(dest, 0, 0, [1, 2, 3, 255]);

		blit(dest, src, { x: 0, y: 0, w: 1, h: 1 }, 0, 0);

		expect(pixelAt(dest, 0, 0)).toEqual([1, 2, 3, 255]);
	});

	it("blit clips at the dest edge without throwing", () => {
		const src = createImage(2, 2);
		setPixel(src, 0, 0, [10, 20, 30, 255]);
		setPixel(src, 1, 0, [40, 50, 60, 255]);
		setPixel(src, 0, 1, [70, 80, 90, 255]);
		setPixel(src, 1, 1, [100, 110, 120, 255]);
		const dest = createImage(2, 2);

		expect(() => blit(dest, src, { x: 0, y: 0, w: 2, h: 2 }, 1, 1)).not.toThrow();

		expect(pixelAt(dest, 1, 1)).toEqual([10, 20, 30, 255]);
		expect(pixelAt(dest, 0, 0)).toEqual([0, 0, 0, 0]);
	});

	it("clipToAlpha clears target pixels where the mask is transparent", () => {
		const target = createImage(2, 1);
		setPixel(target, 0, 0, [10, 20, 30, 255]);
		setPixel(target, 1, 0, [40, 50, 60, 255]);
		const mask = createImage(2, 1);
		setPixel(mask, 0, 0, [0, 0, 0, 255]);

		clipToAlpha(target, mask);

		expect(pixelAt(target, 0, 0)).toEqual([10, 20, 30, 255]);
		expect(pixelAt(target, 1, 0)).toEqual([0, 0, 0, 0]);
	});
});
