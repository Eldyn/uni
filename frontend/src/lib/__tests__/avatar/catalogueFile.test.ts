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

const repoRoot = resolve(dirname(fileURLToPath(import.meta.url)), "../../../../..");
const assetRoot = join(repoRoot, "frontend/public/assets/avatar");

const catalogueDirectory = join(repoRoot, "avatar/catalogue");
const catalogue: Catalogue = parseCatalogue(readMergedCatalogue(catalogueDirectory));

/** PNG IHDR width and height, big-endian at bytes 16 and 20. */
function pngSize(file: string): { width: number; height: number } {
	const bytes = readFileSync(file);
	return { width: bytes.readUInt32BE(16), height: bytes.readUInt32BE(20) };
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

	it("the basic palette has 12 swatches and is a starter palette", () => {
		const basic = catalogue.palettes.find((palette) => palette.id === "basic");
		expect(basic?.starter).toBe(true);
		expect(basic?.swatches.length).toBe(12);
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
			for (const swatch of palette.swatches) {
				expect(swatch.rarity === "basic", swatch.id).toBe(palette.starter === true);
			}
		}
	});
});
