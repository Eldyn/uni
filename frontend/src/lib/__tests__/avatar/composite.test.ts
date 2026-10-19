import { beforeEach, describe, expect, it } from "vitest";
import { setCatalogueForTest } from "$lib/avatar/catalogue";
import { bakeLoadoutPixels, type ImageLookup } from "$lib/avatar/composite";
import { createImage, type PixelImage } from "$lib/avatar/pixels";
import type { ResolvedLayer } from "$lib/avatar/slotRules";
import type { Rgb } from "$lib/avatar/tint";
import { FRAME_SIZE, STRIP_WIDTH, type CatalogueItem } from "$lib/avatar/types";
import { FIXTURE_BOB, avatarFixture } from "./avatarFixture";

const RED: Rgb = [255, 0, 0];

function setPixel(img: PixelImage, x: number, y: number, rgba: number[]): void {
	const offset = (y * img.width + x) * 4;
	img.data.set(rgba, offset);
}

function pixelAt(img: PixelImage, x: number, y: number): number[] {
	const offset = (y * img.width + x) * 4;
	return Array.from(img.data.slice(offset, offset + 4));
}

function stripItem(path: string): CatalogueItem {
	return {
		id: `uni:${path}`,
		slot: "bodywear",
		name: path,
		asset: { kind: "strip", path },
		regions: 1,
		rarity: "common",
		price: 0
	};
}

function stillItem(path: string): CatalogueItem {
	return {
		id: `uni:${path}`,
		slot: "head_top",
		name: path,
		asset: { kind: "still", path, bob: FIXTURE_BOB },
		regions: 1,
		rarity: "common",
		price: 0,
		socket: "head_top"
	};
}

function layer(item: CatalogueItem, colours: Rgb[] = []): ResolvedLayer {
	return { slot: item.slot, item, colours };
}

function lookup(...images: [string, PixelImage][]): ImageLookup {
	const byPath = new Map(images);
	return (path) => byPath.get(path);
}

/** One 480x96 strip with a red pixel at (`localX`, 0) inside frame `frame`. */
function stripWithPixel(frame: number, localX = 0): PixelImage {
	const img = createImage(STRIP_WIDTH, FRAME_SIZE);
	setPixel(img, frame * FRAME_SIZE + localX, 0, [...RED, 255]);
	return img;
}

/** One 96x96 still with a red pixel at (`x`, `y`). */
function stillWithPixel(x: number, y: number): PixelImage {
	const img = createImage(FRAME_SIZE, FRAME_SIZE);
	setPixel(img, x, y, [...RED, 255]);
	return img;
}

/** A strip item with one extra mask file, listed as `${path}.b`. */
function maskedStripItem(path: string): CatalogueItem {
	const item = stripItem(path);
	return { ...item, asset: { kind: "strip", path, masks: [`${path}.b`] }, regions: 6 };
}

describe("bakeLoadoutPixels", () => {
	beforeEach(() => {
		setCatalogueForTest(avatarFixture());
	});

	it("result is 480x96", () => {
		const out = bakeLoadoutPixels([], "uni:round", () => undefined);

		expect(out.width).toBe(STRIP_WIDTH);
		expect(out.height).toBe(FRAME_SIZE);
	});

	it("a strip layer lands in the matching frame", () => {
		const item = stripItem("bodywear/scarf.png");
		const out = bakeLoadoutPixels(
			[layer(item, [RED])],
			"uni:round",
			lookup(["bodywear/scarf.png", stripWithPixel(3, 3)])
		);

		expect(pixelAt(out, 3 * FRAME_SIZE + 3, 0)).toEqual([...RED, 255]);
		expect(pixelAt(out, 3 * FRAME_SIZE, 0)).toEqual([0, 0, 0, 0]);
		expect(pixelAt(out, 0, 0)).toEqual([0, 0, 0, 0]);
	});

	it("tint is applied", () => {
		const item = stripItem("bodywear/scarf.png");
		const out = bakeLoadoutPixels(
			[layer(item, [[10, 20, 30]])],
			"uni:round",
			lookup(["bodywear/scarf.png", stripWithPixel(0)])
		);

		expect(pixelAt(out, 0, 0)).toEqual([10, 20, 30, 255]);
	});

	it("later layers draw over earlier ones", () => {
		const under = stripItem("bodywear/under.png");
		const over = stripItem("bodywear/over.png");
		const out = bakeLoadoutPixels(
			[layer(under, [[1, 2, 3]]), layer(over, [[9, 8, 7]])],
			"uni:round",
			lookup(["bodywear/under.png", stripWithPixel(0)], ["bodywear/over.png", stripWithPixel(0)])
		);

		expect(pixelAt(out, 0, 0)).toEqual([9, 8, 7, 255]);
	});

	it("a still layer is repeated in every frame at socket plus bob", () => {
		const item = stillItem("head_top/cap.png");
		const out = bakeLoadoutPixels(
			[layer(item, [[5, 6, 7]])],
			"uni:round",
			lookup(["head_top/cap.png", stillWithPixel(0, 0)])
		);

		for (let frame = 0; frame < FIXTURE_BOB.length; frame++) {
			const [offsetX, offsetY] = FIXTURE_BOB[frame];
			expect(pixelAt(out, frame * FRAME_SIZE + 8 + offsetX, 4 + offsetY)).toEqual([5, 6, 7, 255]);
		}
	});

	it("a missing image skips the layer without throwing", () => {
		const item = stripItem("bodywear/missing.png");

		let out!: PixelImage;
		expect(() => {
			out = bakeLoadoutPixels([layer(item, [RED])], "uni:round", () => undefined);
		}).not.toThrow();

		expect(out.data.every((value) => value === 0)).toBe(true);
	});

	it("a still layer shifted past the frame edge is clipped inside its frame", () => {
		const item = stillItem("head_top/wide.png");
		const out = bakeLoadoutPixels(
			[layer(item, [RED])],
			"uni:round",
			lookup(["head_top/wide.png", stillWithPixel(95, 0)])
		);

		// 95 + the 8px socket offset lands in the next frame's cell, so it must
		// be dropped rather than bleed into the neighbouring frame.
		expect(pixelAt(out, FRAME_SIZE + 7, 4)).toEqual([0, 0, 0, 0]);
		expect(out.data.every((value) => value === 0)).toBe(true);
	});

	it("an extra mask file tints region 6 over the primary art", () => {
		const item = maskedStripItem("body");
		const primary = createImage(STRIP_WIDTH, FRAME_SIZE);
		setPixel(primary, 2 * FRAME_SIZE, 0, [128, 128, 128, 255]);
		const extra = createImage(STRIP_WIDTH, FRAME_SIZE);
		setPixel(extra, 2 * FRAME_SIZE, 0, [0, 0, 255, 255]);
		const colours: Rgb[] = [RED, RED, RED, RED, RED, [10, 20, 30]];

		const out = bakeLoadoutPixels(
			[layer(item, colours)],
			"uni:round",
			lookup(["body", primary], ["body.b", extra])
		);

		expect(pixelAt(out, 2 * FRAME_SIZE, 0)).toEqual([10, 20, 30, 255]);
	});

	it("a multiply item multiplies grey art by its first color", () => {
		const item = {
			...stripItem("shapes/classic.png"),
			slot: "shape" as const,
			tint: "multiply" as const
		};
		const art = createImage(STRIP_WIDTH, FRAME_SIZE);
		setPixel(art, 0, 0, [237, 237, 237, 255]);
		const out = bakeLoadoutPixels(
			[layer(item, [[232, 74, 74]])],
			"uni:round",
			lookup(["shapes/classic.png", art])
		);

		expect(pixelAt(out, 0, 0)).toEqual([216, 69, 69, 255]);
	});

	it("a missing extra mask image keeps the primary pixel", () => {
		const item = maskedStripItem("body");
		const primary = createImage(STRIP_WIDTH, FRAME_SIZE);
		setPixel(primary, 0, 0, [128, 128, 128, 255]);

		const out = bakeLoadoutPixels([layer(item)], "uni:round", lookup(["body", primary]));

		expect(pixelAt(out, 0, 0)).toEqual([128, 128, 128, 255]);
	});
});
