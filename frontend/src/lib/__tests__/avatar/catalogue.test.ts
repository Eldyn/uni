import { describe, it, expect, beforeEach, vi } from "vitest";

// INFO: the loader imports the merged catalogue from the Vite virtual module.
vi.mock("virtual:avatar-catalogue", () => ({ default: {} }));

import {
	featuredItems,
	findSwatch,
	getCatalogue,
	getItem,
	getSwatch,
	isAvailable,
	isStarter,
	isStarterSwatch,
	resolveItem,
	setCatalogueForTest,
	shopItems,
	slotDefault
} from "$lib/avatar/catalogue";
import type { Catalogue } from "$lib/avatar/types";
import { sixRegionItem } from "./avatarFixture";

const FROM = "2026-06-01T00:00:00Z";
const UNTIL = "2026-07-01T00:00:00Z";

function testCatalogue(): Catalogue {
	return {
		version: 1,
		shape_sockets: {
			"uni:round": {
				eyes: [0, 0],
				mouth: [0, 10],
				head_top: [0, -20],
				body_center: [0, 40]
			}
		},
		palettes: [
			{
				id: "basic",
				name: "Basic",
				price: 0,
				starter: true,
				swatches: [
					{ id: "basic:red", hex: "#e84a4a" },
					{ id: "basic:blue", hex: "#4a6ae8" }
				]
			},
			{
				id: "uni:neon",
				name: "Neon",
				price: 250,
				swatches: [{ id: "uni_neon:pink", hex: "#ff00ff" }]
			}
		],
		items: [
			{
				id: "uni:round",
				slot: "shape",
				name: "Round",
				asset: { kind: "strip", path: "shapes/uni_round.png" },
				regions: 1,
				rarity: "common",
				price: 0,
				starter: true
			},
			{
				id: "uni:eyes_dot",
				slot: "eyes",
				name: "Dot",
				asset: { kind: "strip", path: "eyes/uni_eyes_dot.png" },
				regions: 1,
				rarity: "common",
				price: 0,
				starter: true
			},
			{
				id: "uni:mouth_smile",
				slot: "mouth",
				name: "Smile",
				asset: { kind: "strip", path: "mouth/uni_mouth_smile.png" },
				regions: 1,
				rarity: "common",
				price: 0,
				starter: true
			},
			{
				id: "uni:eyes_wide",
				slot: "eyes",
				name: "Wide",
				asset: { kind: "strip", path: "eyes/uni_eyes_wide.png" },
				regions: 1,
				rarity: "common",
				price: 50
			},
			{
				id: "uni:eyes_hidden",
				slot: "eyes",
				name: "Hidden",
				asset: { kind: "strip", path: "eyes/uni_eyes_hidden.png" },
				regions: 1,
				rarity: "common",
				price: 50,
				hidden: true
			},
			{
				id: "uni:eyes_unlisted",
				slot: "eyes",
				name: "Unlisted",
				asset: { kind: "strip", path: "eyes/uni_eyes_unlisted.png" },
				regions: 1,
				rarity: "common",
				price: 50,
				unlisted: true
			},
			{
				id: "uni:eyes_until",
				slot: "eyes",
				name: "Seasonal",
				asset: { kind: "strip", path: "eyes/uni_eyes_until.png" },
				regions: 1,
				rarity: "common",
				price: 50,
				available_from: FROM,
				available_until: UNTIL
			},
			{
				id: "uni:eyes_from",
				slot: "eyes",
				name: "Upcoming",
				asset: { kind: "strip", path: "eyes/uni_eyes_from.png" },
				regions: 1,
				rarity: "common",
				price: 50,
				available_from: FROM
			},
			{
				id: "uni:mouth_flat",
				slot: "mouth",
				name: "Flat",
				asset: { kind: "strip", path: "mouth/uni_mouth_flat.png" },
				regions: 1,
				rarity: "common",
				price: 30
			},
			{
				id: "uni:hat_cap",
				slot: "head_top",
				name: "Cap",
				asset: {
					kind: "still",
					path: "head_top/uni_hat_cap.png",
					bob: [
						[0, 0],
						[0, -1],
						[0, -2],
						[0, -1],
						[0, 0]
					]
				},
				regions: 0,
				rarity: "common",
				price: 20,
				socket: "head_top"
			}
		],
		featured: ["uni:eyes_wide", "uni:eyes_dot", "uni:eyes_hidden", "uni:eyes_unlisted"]
	};
}

describe("catalogue queries", () => {
	beforeEach(() => {
		setCatalogueForTest(testCatalogue());
	});

	it("getItem finds a known id and returns undefined for an unknown id", () => {
		expect(getItem("uni:eyes_dot")?.id).toBe("uni:eyes_dot");
		expect(getItem("uni:missing")).toBeUndefined();
	});

	it("resolveItem falls back to the slot default for an unknown id", () => {
		expect(resolveItem("uni:missing", "eyes")?.id).toBe("uni:eyes_dot");
	});

	it("resolveItem falls back when the item exists but in another slot", () => {
		expect(resolveItem("uni:mouth_flat", "eyes")?.id).toBe("uni:eyes_dot");
	});

	it("resolveItem keeps the item when its slot matches", () => {
		expect(resolveItem("uni:eyes_wide", "eyes")?.id).toBe("uni:eyes_wide");
	});

	it("slotDefault returns the first starter in the slot", () => {
		expect(slotDefault("mouth")?.id).toBe("uni:mouth_smile");
		expect(slotDefault("head_top")).toBeUndefined();
	});

	it("isStarter is true only for starter items", () => {
		expect(isStarter("uni:eyes_dot")).toBe(true);
		expect(isStarter("uni:eyes_wide")).toBe(false);
		expect(isStarter("uni:missing")).toBe(false);
	});

	it("isAvailable is false exactly at available_until", () => {
		const item = getItem("uni:eyes_until");
		expect(item).toBeDefined();
		expect(isAvailable(item!, new Date(UNTIL))).toBe(false);
		expect(isAvailable(item!, new Date("2026-06-15T00:00:00Z"))).toBe(true);
	});

	it("isAvailable is true exactly at available_from", () => {
		const item = getItem("uni:eyes_from");
		expect(item).toBeDefined();
		expect(isAvailable(item!, new Date(FROM))).toBe(true);
		expect(isAvailable(item!, new Date("2026-05-01T00:00:00Z"))).toBe(false);
	});

	it("shopItems hides starters, hidden and unlisted items", () => {
		const ids = shopItems(new Date("2026-06-15T00:00:00Z")).map((item) => item.id);
		expect(ids).toContain("uni:eyes_wide");
		expect(ids).toContain("uni:eyes_until");
		expect(ids).not.toContain("uni:round");
		expect(ids).not.toContain("uni:eyes_dot");
		expect(ids).not.toContain("uni:mouth_smile");
		expect(ids).not.toContain("uni:eyes_hidden");
		expect(ids).not.toContain("uni:eyes_unlisted");
	});

	it("featuredItems keeps only shop-visible featured ids", () => {
		const ids = featuredItems(new Date("2026-06-15T00:00:00Z")).map((item) => item.id);
		expect(ids).toEqual(["uni:eyes_wide"]);
	});

	it("getSwatch finds a swatch by id and returns undefined for an unknown id", () => {
		expect(getSwatch("basic:red")?.hex).toBe("#e84a4a");
		expect(getSwatch("nope:nope")).toBeUndefined();
	});

	it("findSwatch returns the swatch and its palette", () => {
		expect(findSwatch("uni_neon:pink")?.palette.id).toBe("uni:neon");
		expect(findSwatch("nope:nope")).toBeUndefined();
	});

	it("isStarterSwatch is true for basic palette swatches and false for a paid palette", () => {
		expect(isStarterSwatch("basic:red")).toBe(true);
		expect(isStarterSwatch("uni_neon:pink")).toBe(false);
		expect(isStarterSwatch("nope:nope")).toBe(false);
	});

	it("getCatalogue returns the installed catalogue", () => {
		expect(getCatalogue().version).toBe(1);
	});

	it("keeps the extra mask files and the region count of an item", () => {
		const catalogue = testCatalogue();
		catalogue.items.push(sixRegionItem());
		setCatalogueForTest(catalogue);

		const item = getItem("uni:motif_rainbow");
		expect(item?.regions).toBe(6);
		expect(item?.asset.masks).toEqual(["motif/uni_motif_rainbow_b.png"]);
	});
});
