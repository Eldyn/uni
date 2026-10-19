import { describe, it, expect } from "vitest";
import { createImage, type PixelImage } from "$lib/avatar/pixels";
import { hexToRgb, tintImage, type Rgb } from "$lib/avatar/tint";

function setPixel(img: PixelImage, x: number, y: number, rgba: number[]): void {
	const offset = (y * img.width + x) * 4;
	img.data.set(rgba, offset);
}

function pixelAt(img: PixelImage, x: number, y: number): number[] {
	const offset = (y * img.width + x) * 4;
	return Array.from(img.data.slice(offset, offset + 4));
}

function imageWith(rgba: number[]): PixelImage {
	const img = createImage(1, 1);
	setPixel(img, 0, 0, rgba);
	return img;
}

const RED: Rgb = [10, 20, 30];

describe("hexToRgb", () => {
	it("parses #ff8000", () => {
		expect(hexToRgb("#ff8000")).toEqual([255, 128, 0]);
	});
});

describe("tintImage", () => {
	it("tints region 0 pixel (255,0,0) to the colour", () => {
		const out = tintImage(imageWith([255, 0, 0, 255]), [RED]);

		expect(pixelAt(out, 0, 0)).toEqual([10, 20, 30, 255]);
	});

	it("scales by shade: (128,0,0) with colour (200,100,50) gives about (100,50,25)", () => {
		const out = tintImage(imageWith([128, 0, 0, 255]), [[200, 100, 50]]);

		expect(pixelAt(out, 0, 0)).toEqual([100, 50, 25, 255]);
	});

	it("uses colours[1] for the green region", () => {
		const out = tintImage(imageWith([0, 255, 0, 255]), [[1, 2, 3], RED]);

		expect(pixelAt(out, 0, 0)).toEqual([10, 20, 30, 255]);
	});

	it("keeps grey pixels unchanged", () => {
		const out = tintImage(imageWith([128, 128, 128, 255]), [RED]);

		expect(pixelAt(out, 0, 0)).toEqual([128, 128, 128, 255]);
	});

	it("keeps clear pixels clear", () => {
		const out = tintImage(imageWith([0, 0, 0, 0]), [RED]);

		expect(pixelAt(out, 0, 0)).toEqual([0, 0, 0, 0]);
	});

	it("turns an invalid pixel clear", () => {
		const out = tintImage(imageWith([255, 255, 0, 255]), [RED]);

		expect(pixelAt(out, 0, 0)).toEqual([0, 0, 0, 0]);
	});

	it("with a missing colour gives the shade as grey", () => {
		const out = tintImage(imageWith([128, 0, 0, 255]), []);

		expect(pixelAt(out, 0, 0)).toEqual([128, 128, 128, 255]);
	});

	it("does not mutate the source", () => {
		const src = imageWith([200, 0, 0, 255]);
		const before = Array.from(src.data);

		tintImage(src, [RED]);

		expect(Array.from(src.data)).toEqual(before);
	});
});

describe("tintImage with an extra mask file", () => {
	const FILLER: Rgb = [0, 0, 0];
	const REGION_FOUR: Rgb = [200, 100, 50];

	it("an extra red pixel tints region 4 over an opaque primary pixel", () => {
		const colours = [FILLER, FILLER, FILLER, REGION_FOUR];
		const out = tintImage(imageWith([128, 128, 128, 255]), colours, [imageWith([255, 0, 0, 255])]);

		expect(pixelAt(out, 0, 0)).toEqual([200, 100, 50, 255]);
	});

	it("scales region 4 by the extra shade", () => {
		const colours = [FILLER, FILLER, FILLER, REGION_FOUR];
		const out = tintImage(imageWith([255, 0, 0, 255]), colours, [imageWith([128, 0, 0, 255])]);

		expect(pixelAt(out, 0, 0)).toEqual([100, 50, 25, 255]);
	});

	it("an extra blue pixel tints region 6", () => {
		const colours = [FILLER, FILLER, FILLER, FILLER, FILLER, REGION_FOUR];
		const out = tintImage(imageWith([128, 128, 128, 255]), colours, [imageWith([0, 0, 255, 255])]);

		expect(pixelAt(out, 0, 0)).toEqual([200, 100, 50, 255]);
	});

	it("a region with no colour falls back to grey of its shade", () => {
		const out = tintImage(imageWith([128, 128, 128, 255]), [], [imageWith([0, 255, 0, 255])]);

		expect(pixelAt(out, 0, 0)).toEqual([255, 255, 255, 255]);
	});

	it("never makes a clear primary pixel opaque", () => {
		const colours = [FILLER, FILLER, FILLER, REGION_FOUR];
		const out = tintImage(imageWith([0, 0, 0, 0]), colours, [imageWith([255, 0, 0, 255])]);

		expect(pixelAt(out, 0, 0)).toEqual([0, 0, 0, 0]);
	});

	it("ignores a grey extra pixel", () => {
		const out = tintImage(imageWith([255, 0, 0, 255]), [RED], [imageWith([90, 90, 90, 255])]);

		expect(pixelAt(out, 0, 0)).toEqual([10, 20, 30, 255]);
	});

	it("ignores a clear or invalid extra pixel", () => {
		const clear = tintImage(imageWith([255, 0, 0, 255]), [RED], [imageWith([0, 0, 0, 0])]);
		const invalid = tintImage(imageWith([255, 0, 0, 255]), [RED], [imageWith([255, 255, 0, 255])]);

		expect(pixelAt(clear, 0, 0)).toEqual([10, 20, 30, 255]);
		expect(pixelAt(invalid, 0, 0)).toEqual([10, 20, 30, 255]);
	});

	it("ignores a missing extra image and an extra image of another size", () => {
		const primary = imageWith([255, 0, 0, 255]);
		const wrongSize = createImage(2, 1);

		expect(pixelAt(tintImage(primary, [RED], [undefined]), 0, 0)).toEqual([10, 20, 30, 255]);
		expect(pixelAt(tintImage(primary, [RED], [wrongSize]), 0, 0)).toEqual([10, 20, 30, 255]);
	});

	it("without extra files the result is the same as before", () => {
		const out = tintImage(imageWith([255, 0, 0, 255]), [RED]);

		expect(pixelAt(out, 0, 0)).toEqual([10, 20, 30, 255]);
	});
});
