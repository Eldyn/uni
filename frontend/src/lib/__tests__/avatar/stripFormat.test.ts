import { describe, it, expect } from "vitest";
import { createImage, type PixelImage } from "$lib/avatar/pixels";
import { classifyPixel, findMaskProblems, isStillSize, isStripSize } from "$lib/avatar/stripFormat";

function setPixel(img: PixelImage, x: number, y: number, rgba: number[]): void {
	const offset = (y * img.width + x) * 4;
	img.data.set(rgba, offset);
}

describe("classifyPixel", () => {
	it("maps alpha 0 to clear", () => {
		expect(classifyPixel(12, 34, 56, 0)).toEqual({ kind: "clear" });
	});

	it("maps (128,128,128,255) to grey", () => {
		expect(classifyPixel(128, 128, 128, 255)).toEqual({ kind: "grey", value: 128 });
	});

	it("maps (0,0,0,255) to grey 0", () => {
		expect(classifyPixel(0, 0, 0, 255)).toEqual({ kind: "grey", value: 0 });
	});

	it("maps (200,0,0,255) to region 0 with shade 200/255", () => {
		const pixel = classifyPixel(200, 0, 0, 255);
		expect(pixel.kind).toBe("region");
		if (pixel.kind !== "region") return;
		expect(pixel.index).toBe(0);
		expect(pixel.shade).toBeCloseTo(200 / 255);
	});

	it("maps (0,90,0,255) to region 1", () => {
		const pixel = classifyPixel(0, 90, 0, 255);
		expect(pixel.kind).toBe("region");
		if (pixel.kind !== "region") return;
		expect(pixel.index).toBe(1);
		expect(pixel.shade).toBeCloseTo(90 / 255);
	});

	it("maps (0,0,40,255) to region 2", () => {
		const pixel = classifyPixel(0, 0, 40, 255);
		expect(pixel.kind).toBe("region");
		if (pixel.kind !== "region") return;
		expect(pixel.index).toBe(2);
		expect(pixel.shade).toBeCloseTo(40 / 255);
	});

	it("maps (200,0,0,255) in file 1 to region 3", () => {
		const pixel = classifyPixel(200, 0, 0, 255, 1);
		expect(pixel.kind).toBe("region");
		if (pixel.kind !== "region") return;
		expect(pixel.index).toBe(3);
		expect(pixel.shade).toBeCloseTo(200 / 255);
	});

	it("maps (0,90,0,255) in file 1 to region 4", () => {
		const pixel = classifyPixel(0, 90, 0, 255, 1);
		expect(pixel.kind === "region" && pixel.index).toBe(4);
	});

	it("maps (0,0,40,255) in file 1 to region 5", () => {
		const pixel = classifyPixel(0, 0, 40, 255, 1);
		expect(pixel.kind === "region" && pixel.index).toBe(5);
	});

	it("keeps clear, grey and invalid the same in file 1", () => {
		expect(classifyPixel(1, 2, 3, 0, 1)).toEqual({ kind: "clear" });
		expect(classifyPixel(128, 128, 128, 255, 1)).toEqual({ kind: "grey", value: 128 });
		expect(classifyPixel(255, 255, 0, 255, 1)).toEqual({ kind: "invalid" });
	});

	it("maps (255,255,0,255) to invalid", () => {
		expect(classifyPixel(255, 255, 0, 255)).toEqual({ kind: "invalid" });
	});

	it("maps alpha 128 to invalid", () => {
		expect(classifyPixel(200, 0, 0, 128)).toEqual({ kind: "invalid" });
	});
});

describe("strip sizes", () => {
	it("isStripSize accepts 480x96 only", () => {
		expect(isStripSize(createImage(480, 96))).toBe(true);
		expect(isStripSize(createImage(96, 96))).toBe(false);
		expect(isStripSize(createImage(480, 95))).toBe(false);
	});

	it("isStillSize accepts 96x96 only", () => {
		expect(isStillSize(createImage(96, 96))).toBe(true);
		expect(isStillSize(createImage(480, 96))).toBe(false);
		expect(isStillSize(createImage(96, 95))).toBe(false);
	});
});

describe("findMaskProblems", () => {
	it("reports x,y and reason and respects limit", () => {
		const img = createImage(3, 2);
		setPixel(img, 1, 0, [255, 255, 0, 255]); // mixed colour
		setPixel(img, 0, 1, [200, 0, 0, 128]); // partial alpha
		setPixel(img, 2, 1, [0, 90, 0, 128]); // partial alpha
		setPixel(img, 0, 0, [200, 0, 0, 255]); // valid

		expect(findMaskProblems(img)).toEqual([
			{ x: 1, y: 0, reason: "mixed-colour" },
			{ x: 0, y: 1, reason: "partial-alpha" },
			{ x: 2, y: 1, reason: "partial-alpha" }
		]);
		expect(findMaskProblems(img, 2)).toEqual([
			{ x: 1, y: 0, reason: "mixed-colour" },
			{ x: 0, y: 1, reason: "partial-alpha" }
		]);
		expect(findMaskProblems(createImage(3, 2))).toEqual([]);
	});
});
