import { describe, expect, it } from "vitest";
import { parseCatalogue } from "$lib/avatar/catalogueSchema";
import type { Catalogue } from "$lib/avatar/types";
// @ts-expect-error plain ESM helper without types
import {
	listCatalogueFiles,
	readMergedCatalogue
} from "../../../../scripts/lib/avatarCatalogue.mjs";

// The catalogue and its art live outside the Vite root, so they are read from
// disk through Node built-ins; the browser condition vitest runs with would
// otherwise shadow node:fs (same pattern as the other repo-wide file tests).
const nodeModule = (name: string) => import(/* @vite-ignore */ `node:${name}`);
const { existsSync, readFileSync } = await nodeModule("fs");
const { dirname, join, resolve } = await nodeModule("path");
const { fileURLToPath } = await nodeModule("url");
const { inflateSync } = await nodeModule("zlib");

const repoRoot = resolve(dirname(fileURLToPath(import.meta.url)), "../../../../..");
const assetRoot = join(repoRoot, "frontend/public/assets/avatar");

const catalogueDirectory = join(repoRoot, "avatar/catalogue");
const catalogue: Catalogue = parseCatalogue(readMergedCatalogue(catalogueDirectory));

/** PNG IHDR width and height, big-endian at bytes 16 and 20. */
function pngSize(file: string): { width: number; height: number } {
	const bytes = readFileSync(file);
	return { width: bytes.readUInt32BE(16), height: bytes.readUInt32BE(20) };
}

/** Decodes an 8-bit RGBA PNG, undoing the per-row filters. */
function pngRgba(file: string): { width: number; height: number; pixels: Uint8Array } {
	const bytes = readFileSync(file);
	const idat: Buffer[] = [];
	let width = 0;
	let height = 0;
	for (let offset = 8; offset < bytes.length;) {
		const length = bytes.readUInt32BE(offset);
		const type = bytes.toString("ascii", offset + 4, offset + 8);
		const data = bytes.subarray(offset + 8, offset + 8 + length);
		if (type === "IHDR") {
			width = data.readUInt32BE(0);
			height = data.readUInt32BE(4);
			expect(data[8], "PNG bit depth").toBe(8);
			expect(data[9], "PNG color type").toBe(6);
		} else if (type === "IDAT") {
			idat.push(data);
		} else if (type === "IEND") {
			break;
		}
		offset += 12 + length;
	}
	const raw: Uint8Array = inflateSync(Buffer.concat(idat));
	const stride = width * 4;
	const pixels = new Uint8Array(stride * height);
	for (let y = 0; y < height; y += 1) {
		const start = y * (stride + 1);
		const filter = raw[start];
		for (let x = 0; x < stride; x += 1) {
			const left = x >= 4 ? pixels[y * stride + x - 4] : 0;
			const up = y > 0 ? pixels[(y - 1) * stride + x] : 0;
			const upLeft = x >= 4 && y > 0 ? pixels[(y - 1) * stride + x - 4] : 0;
			let value = raw[start + 1 + x];
			if (filter === 1) value += left;
			else if (filter === 2) value += up;
			else if (filter === 3) value += Math.floor((left + up) / 2);
			else if (filter === 4) {
				const p = left + up - upLeft;
				const pa = Math.abs(p - left);
				const pb = Math.abs(p - up);
				const pc = Math.abs(p - upLeft);
				value += pa <= pb && pa <= pc ? left : pb <= pc ? up : upLeft;
			}
			pixels[y * stride + x] = value & 0xff;
		}
	}
	return { width, height, pixels };
}

/** Counts per slot for the launch slice in the design, section 4. */
const LAUNCH_COUNTS: Record<string, number> = {
	shape: 3,
	eyes: 3,
	mouth: 3,
	head_top: 2,
	mask: 1,
	bodywear: 1,
	set: 1,
	motif: 2
};

describe("avatar catalogue file", () => {
	it("every file name is lowercase letters, digits and underscores", () => {
		const bad = listCatalogueFiles(catalogueDirectory).filter(
			(path: string) => !/^[a-z0-9_/]+\.json$/.test(path)
		);
		expect(bad).toEqual([]);
	});

	it("real catalogue parses with parseCatalogue", () => {
		expect(catalogue.version).toBe(1);
		expect(catalogue.items.length).toBe(16);
	});

	it("the default body is the original UNI body, tinted by multiply", () => {
		const firstShapeStarter = catalogue.items.find(
			(item) => item.slot === "shape" && item.starter === true
		);
		expect(firstShapeStarter?.id).toBe("uni:classic");
		expect(firstShapeStarter?.regions).toBe(1);
		expect(firstShapeStarter?.tint).toBe("multiply");
	});

	it("every colored part and motif lists one default color per stored color", () => {
		const wrong = catalogue.items
			.filter((item) => item.slot !== "shape")
			.filter((item) => {
				const expected = item.slot === "motif" ? Math.max(0, item.regions - 1) : item.regions;
				return (item.default_colors ?? []).length !== expected;
			})
			.map((item) => item.id);
		expect(wrong).toEqual([]);
	});

	it("an item with no regions has no default colors", () => {
		const bad = catalogue.items.filter(
			(item) => item.regions === 0 && (item.default_colors ?? []).length > 0
		);
		expect(bad).toEqual([]);
	});

	it("the dot eyes and the smile are flat and non-colorable", () => {
		for (const id of ["uni:eyes_dot", "uni:mouth_smile"]) {
			const item = catalogue.items.find((candidate) => candidate.id === id);
			expect(item, id).toBeDefined();
			expect(item?.regions, id).toBe(0);
			expect(item?.tint, id).toBe("none");
			expect(item?.default_colors, id).toBeUndefined();
		}
	});

	it("every item asset file exists under frontend/public/assets/avatar", () => {
		const missing = catalogue.items
			.map((item) => join(assetRoot, item.asset.path))
			.filter((file) => !existsSync(file));
		expect(missing).toEqual([]);
	});

	it("every extra mask file exists, has the primary size and the item has enough regions", () => {
		const problems = catalogue.items.flatMap((item) => {
			const masks = item.asset.masks ?? [];
			const found: string[] = [];
			if (item.regions > 3 * (1 + masks.length)) found.push(`${item.id}: too few masks`);
			for (const mask of masks) {
				const file = join(assetRoot, mask);
				if (!existsSync(file)) {
					found.push(`${item.id}: missing ${mask}`);
					continue;
				}
				const primary = pngSize(join(assetRoot, item.asset.path));
				const size = pngSize(file);
				if (size.width !== primary.width || size.height !== primary.height) {
					found.push(`${item.id}: ${mask} has another size than ${item.asset.path}`);
				}
			}
			return found;
		});
		expect(problems).toEqual([]);
	});

	it("strip assets are 480x96 and still assets are 96x96", () => {
		const wrong = catalogue.items
			.filter((item) => {
				const { width, height } = pngSize(join(assetRoot, item.asset.path));
				return item.asset.kind === "strip"
					? width !== 480 || height !== 96
					: width !== 96 || height !== 96;
			})
			.map((item) => item.id);
		expect(wrong).toEqual([]);
	});

	it("the basic palette has 13 swatches and is a starter palette", () => {
		const basic = catalogue.palettes.find((palette) => palette.id === "basic");
		expect(basic?.starter).toBe(true);
		expect(basic?.swatches.length).toBe(13);
	});

	it("the basic palette holds the hidden reset swatch", () => {
		const basic = catalogue.palettes.find((palette) => palette.id === "basic");
		const reset = basic?.swatches.find((swatch) => swatch.id === "basic:none");

		expect(reset?.pattern).toBe("none");
		expect(reset?.hidden).toBe(true);
	});

	it("launch counts match the spec slice", () => {
		const counts: Record<string, number> = {};
		for (const item of catalogue.items) {
			counts[item.slot] = (counts[item.slot] ?? 0) + 1;
		}
		expect(counts).toEqual(LAUNCH_COUNTS);
	});

	it("featured ids all exist", () => {
		expect(catalogue.featured.length).toBe(3);
		const ids = new Set(catalogue.items.map((item) => item.id));
		expect(catalogue.featured.filter((id) => !ids.has(id))).toEqual([]);
	});

	it("the paid swatches use the rarity price table", () => {
		const paid = catalogue.palettes.flatMap((palette) =>
			palette.swatches
				.filter((swatch) => swatch.price !== undefined)
				.map((swatch) => [swatch.id, swatch.price, swatch.rarity])
		);
		expect(paid).toEqual([
			["uni:pastel:blush", 100, "common"],
			["uni:pastel:sky", 250, "rare"],
			["uni:neon:volt", 600, "epic"],
			["uni:neon:plasma", 1200, "epic"]
		]);
	});

	it("every starter item and starter swatch is basic, and nothing else is", () => {
		const starterItems = catalogue.items.filter((item) => item.starter === true);
		expect(starterItems.every((item) => item.rarity === "basic")).toBe(true);
		const others = catalogue.items.filter((item) => item.starter !== true);
		expect(others.some((item) => item.rarity === "basic")).toBe(false);
		for (const palette of catalogue.palettes) {
			for (const swatch of palette.swatches.filter((entry) => entry.pattern !== "none")) {
				expect(swatch.rarity === "basic", swatch.id).toBe(palette.starter === true);
			}
		}
	});

	it("the stripes motif has a skin base and one stripe region", () => {
		const stripes = catalogue.items.find((item) => item.id === "uni:motif_stripes");
		expect(stripes?.regions).toBe(2);
		expect(stripes?.default_colors).toHaveLength(1);
	});

	it("motif art is a 480x96 strip of opaque full-square frames with hard alpha", () => {
		for (const id of ["uni:motif_stripes", "uni:motif_patches"]) {
			const item = catalogue.items.find((candidate) => candidate.id === id);
			expect(item, id).toBeDefined();
			const { width, height, pixels } = pngRgba(join(assetRoot, item!.asset.path));
			expect({ width, height }, id).toEqual({ width: 480, height: 96 });
			const frameCount = width / 96;
			expect(frameCount, id).toBe(5);
			for (let frame = 0; frame < frameCount; frame += 1) {
				let opaque = 0;
				const softAlpha: string[] = [];
				for (let y = 0; y < 96; y += 1) {
					for (let x = 0; x < 96; x += 1) {
						const alpha = pixels[(y * width + frame * 96 + x) * 4 + 3];
						if (alpha === 255) opaque += 1;
						else if (alpha !== 0) softAlpha.push(`${x},${y}=${alpha}`);
					}
				}
				expect(softAlpha, `${id} frame ${frame} has partial alpha`).toEqual([]);
				expect(opaque, `${id} frame ${frame} does not cover the 96x96 cell`).toBe(96 * 96);
			}
		}
	});
});
