import { describe, it, expect } from "vitest";
import { ZodError } from "zod";
import { parseCatalogue } from "$lib/avatar/catalogueSchema";

function validCatalogue(): Record<string, unknown> {
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
				swatches: [{ id: "basic:red", hex: "#e84a4a" }]
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
				name: "Dot eyes",
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
			}
		],
		featured: ["uni:eyes_dot"]
	};
}

function items(catalogue: Record<string, unknown>): Record<string, unknown>[] {
	return catalogue.items as Record<string, unknown>[];
}

describe("parseCatalogue", () => {
	it("accepts a minimal valid catalogue", () => {
		const catalogue = parseCatalogue(validCatalogue());
		expect(catalogue.version).toBe(1);
		expect(catalogue.items).toHaveLength(3);
		expect(catalogue.featured).toEqual(["uni:eyes_dot"]);
	});

	it("rejects an item id without a namespace colon", () => {
		const catalogue = validCatalogue();
		items(catalogue)[1].id = "eyes_dot";
		expect(() => parseCatalogue(catalogue)).toThrow(ZodError);
	});

	it("rejects duplicate item ids", () => {
		const catalogue = validCatalogue();
		items(catalogue).push({ ...items(catalogue)[1] });
		expect(() => parseCatalogue(catalogue)).toThrow(ZodError);
	});

	it("accepts 4 regions when one extra mask file is listed", () => {
		const catalogue = validCatalogue();
		const eyes = items(catalogue)[1];
		eyes.asset = { kind: "strip", path: "eyes/uni_eyes_dot.png", masks: ["eyes/b.png"] };
		eyes.regions = 4;
		expect(parseCatalogue(catalogue).items[1].regions).toBe(4);
	});

	it("rejects 4 regions with no extra mask file", () => {
		const catalogue = validCatalogue();
		items(catalogue)[1].regions = 4;
		expect(() => parseCatalogue(catalogue)).toThrow(ZodError);
	});

	it("rejects regions above 6", () => {
		const catalogue = validCatalogue();
		const eyes = items(catalogue)[1];
		eyes.asset = { kind: "strip", path: "eyes/uni_eyes_dot.png", masks: ["eyes/b.png"] };
		eyes.regions = 7;
		expect(() => parseCatalogue(catalogue)).toThrow(ZodError);
	});

	it("rejects more than one extra mask file", () => {
		const catalogue = validCatalogue();
		items(catalogue)[1].asset = {
			kind: "strip",
			path: "eyes/uni_eyes_dot.png",
			masks: ["a.png", "b.png"]
		};
		expect(() => parseCatalogue(catalogue)).toThrow(ZodError);
	});

	it("rejects a masks entry that is not a string", () => {
		const catalogue = validCatalogue();
		items(catalogue)[1].asset = { kind: "strip", path: "eyes/uni_eyes_dot.png", masks: [7] };
		expect(() => parseCatalogue(catalogue)).toThrow(ZodError);
	});

	it("rejects a negative or fractional price", () => {
		const negative = validCatalogue();
		items(negative)[1].price = -1;
		expect(() => parseCatalogue(negative)).toThrow(ZodError);

		const fractional = validCatalogue();
		items(fractional)[1].price = 1.5;
		expect(() => parseCatalogue(fractional)).toThrow(ZodError);
	});

	it("rejects a still asset whose bob has not exactly 5 entries", () => {
		const catalogue = validCatalogue();
		items(catalogue).push({
			id: "uni:hat_cap",
			slot: "head_top",
			name: "Cap",
			asset: {
				kind: "still",
				path: "head_top/uni_hat_cap.png",
				bob: [
					[0, 0],
					[0, -1]
				]
			},
			regions: 0,
			rarity: "common",
			price: 0,
			socket: "head_top"
		});
		expect(() => parseCatalogue(catalogue)).toThrow(ZodError);
	});

	it("rejects a swatch hex that is not #rrggbb", () => {
		const catalogue = validCatalogue();
		const palettes = catalogue.palettes as { swatches: { hex: string }[] }[];
		palettes[0].swatches[0].hex = "e84a4a";
		expect(() => parseCatalogue(catalogue)).toThrow(ZodError);
	});

	it("rejects an unknown slot", () => {
		const catalogue = validCatalogue();
		items(catalogue)[1].slot = "hat";
		expect(() => parseCatalogue(catalogue)).toThrow(ZodError);
	});

	it("rejects a catalogue with no starter for shape, eyes or mouth", () => {
		const catalogue = validCatalogue();
		for (const item of items(catalogue)) delete item.starter;
		expect(() => parseCatalogue(catalogue)).toThrow(ZodError);
	});

	it("rejects a featured id that is not an item", () => {
		const catalogue = validCatalogue();
		catalogue.featured = ["uni:eyes_dot", "uni:missing"];
		expect(() => parseCatalogue(catalogue)).toThrow(ZodError);
	});

	it("rejects a shape_sockets entry missing a socket", () => {
		const catalogue = validCatalogue();
		const sockets = catalogue.shape_sockets as Record<string, Record<string, unknown>>;
		delete sockets["uni:round"].body_center;
		expect(() => parseCatalogue(catalogue)).toThrow(ZodError);
	});

	it("rejects available_until earlier than available_from", () => {
		const catalogue = validCatalogue();
		items(catalogue)[1].available_from = "2027-01-01T00:00:00Z";
		items(catalogue)[1].available_until = "2026-01-01T00:00:00Z";
		expect(() => parseCatalogue(catalogue)).toThrow(ZodError);
	});

	it("rejects a swatch id duplicated across palettes", () => {
		const catalogue = validCatalogue();
		const palettes = catalogue.palettes as unknown[];
		palettes.push({
			id: "uni:neon",
			name: "Neon",
			price: 250,
			swatches: [{ id: "basic:red", hex: "#00ff00" }]
		});
		expect(() => parseCatalogue(catalogue)).toThrow(ZodError);
	});
});

function itemOf(catalogue: Record<string, unknown>, id: string): Record<string, unknown> {
	const items = catalogue.items as Record<string, unknown>[];
	const found = items.find((item) => item.id === id);
	if (!found) throw new Error(`no item ${id}`);
	return found;
}

describe("tint", () => {
	it("accepts multiply on a one-region item", () => {
		const catalogue = validCatalogue();
		itemOf(catalogue, "uni:round").tint = "multiply";
		expect(parseCatalogue(catalogue).items[0].tint).toBe("multiply");
	});

	it("rejects an unknown tint mode", () => {
		const catalogue = validCatalogue();
		itemOf(catalogue, "uni:round").tint = "paint";
		expect(() => parseCatalogue(catalogue)).toThrow(ZodError);
	});

	it("rejects multiply with no region", () => {
		const catalogue = validCatalogue();
		const item = itemOf(catalogue, "uni:round");
		item.tint = "multiply";
		item.regions = 0;
		expect(() => parseCatalogue(catalogue)).toThrow(ZodError);
	});

	it("rejects multiply with two regions", () => {
		const catalogue = validCatalogue();
		const item = itemOf(catalogue, "uni:round");
		item.tint = "multiply";
		item.regions = 2;
		expect(() => parseCatalogue(catalogue)).toThrow(ZodError);
	});

	it("rejects multiply with an extra mask file", () => {
		const catalogue = validCatalogue();
		const item = itemOf(catalogue, "uni:round");
		item.tint = "multiply";
		item.asset = { kind: "strip", path: "shapes/uni_round.png", masks: ["shapes/b.png"] };
		expect(() => parseCatalogue(catalogue)).toThrow(ZodError);
	});

	it("accepts none on an item with no region", () => {
		const catalogue = validCatalogue();
		const item = itemOf(catalogue, "uni:eyes_dot");
		item.tint = "none";
		item.regions = 0;
		expect(parseCatalogue(catalogue).items[1].tint).toBe("none");
	});

	it("rejects none with a region", () => {
		const catalogue = validCatalogue();
		itemOf(catalogue, "uni:eyes_dot").tint = "none";
		expect(() => parseCatalogue(catalogue)).toThrow(ZodError);
	});

	it("rejects none with an extra mask file", () => {
		const catalogue = validCatalogue();
		const item = itemOf(catalogue, "uni:eyes_dot");
		item.tint = "none";
		item.regions = 0;
		item.asset = { kind: "strip", path: "eyes/uni_eyes_dot.png", masks: ["eyes/b.png"] };
		expect(() => parseCatalogue(catalogue)).toThrow(ZodError);
	});

	it("rejects none with default colors, even an empty list", () => {
		const catalogue = validCatalogue();
		const item = itemOf(catalogue, "uni:eyes_dot");
		item.tint = "none";
		item.regions = 0;
		item.default_colors = [];
		expect(() => parseCatalogue(catalogue)).toThrow(ZodError);
	});
});

describe("default_colors", () => {
	it("accepts a swatch id and a hex entry", () => {
		const catalogue = validCatalogue();
		const eyes = itemOf(catalogue, "uni:eyes_dot");
		eyes.regions = 2;
		eyes.default_colors = ["basic:red", "#202020"];
		expect(parseCatalogue(catalogue).items[1].default_colors).toEqual(["basic:red", "#202020"]);
	});

	it("accepts an item with no regions and no default colors", () => {
		const catalogue = validCatalogue();
		(catalogue.items as unknown[]).push({
			id: "uni:set_suit",
			slot: "set",
			name: "Suit",
			asset: { kind: "strip", path: "set/uni_set_suit.png" },
			regions: 0,
			rarity: "rare",
			price: 900
		});
		expect(() => parseCatalogue(catalogue)).not.toThrow();
	});

	it("rejects a swatch id that no palette holds", () => {
		const catalogue = validCatalogue();
		itemOf(catalogue, "uni:eyes_dot").default_colors = ["basic:nope"];
		expect(() => parseCatalogue(catalogue)).toThrow(ZodError);
	});

	it("rejects a bad hex entry", () => {
		const catalogue = validCatalogue();
		itemOf(catalogue, "uni:eyes_dot").default_colors = ["#12"];
		expect(() => parseCatalogue(catalogue)).toThrow(ZodError);
	});

	it("rejects more entries than the item has regions", () => {
		const catalogue = validCatalogue();
		itemOf(catalogue, "uni:eyes_dot").default_colors = ["basic:red", "basic:red"];
		expect(() => parseCatalogue(catalogue)).toThrow(ZodError);
	});

	it("rejects a non-empty list on an item with no regions", () => {
		const catalogue = validCatalogue();
		const eyes = itemOf(catalogue, "uni:eyes_dot");
		eyes.regions = 0;
		eyes.default_colors = ["basic:red"];
		expect(() => parseCatalogue(catalogue)).toThrow(ZodError);
	});

	it("rejects default colors on a shape", () => {
		const catalogue = validCatalogue();
		itemOf(catalogue, "uni:round").default_colors = ["basic:red"];
		expect(() => parseCatalogue(catalogue)).toThrow(ZodError);
	});

	it("a motif holds one entry less than its regions", () => {
		const catalogue = validCatalogue();
		(catalogue.items as unknown[]).push({
			id: "uni:motif_x",
			slot: "motif",
			name: "X",
			asset: { kind: "strip", path: "motif/x.png" },
			regions: 2,
			rarity: "rare",
			price: 100,
			default_colors: ["basic:red", "basic:red"]
		});
		expect(() => parseCatalogue(catalogue)).toThrow(ZodError);
	});
});

describe("swatch metadata", () => {
	function swatches(catalogue: Record<string, unknown>): Record<string, unknown>[] {
		return (catalogue.palettes as { swatches: Record<string, unknown>[] }[])[0].swatches;
	}

	it("accepts a price, rarity, hidden and unlisted on a swatch", () => {
		const catalogue = validCatalogue();
		const swatch = swatches(catalogue)[0];
		swatch.price = 250;
		swatch.rarity = "special";
		swatch.hidden = true;
		swatch.unlisted = true;

		const parsed = parseCatalogue(catalogue).palettes[0].swatches[0];
		expect(parsed.price).toBe(250);
		expect(parsed.rarity).toBe("special");
		expect(parsed.hidden).toBe(true);
		expect(parsed.unlisted).toBe(true);
	});

	it("rejects an unknown rarity", () => {
		const catalogue = validCatalogue();
		swatches(catalogue)[0].rarity = "mythic";
		expect(() => parseCatalogue(catalogue)).toThrow(ZodError);
	});

	it("rejects the old uncommon rarity", () => {
		const catalogue = validCatalogue();
		(catalogue.items as Record<string, unknown>[])[0].rarity = "uncommon";
		expect(() => parseCatalogue(catalogue)).toThrow(ZodError);
	});

	it("accepts basic and special on an item and a swatch", () => {
		const catalogue = validCatalogue();
		(catalogue.items as Record<string, unknown>[])[0].rarity = "special";
		swatches(catalogue)[0].rarity = "basic";
		const parsed = parseCatalogue(catalogue);
		expect(parsed.items[0].rarity).toBe("special");
		expect(parsed.palettes[0].swatches[0].rarity).toBe("basic");
	});

	it("rejects a negative or fractional price", () => {
		const negative = validCatalogue();
		swatches(negative)[0].price = -1;
		expect(() => parseCatalogue(negative)).toThrow(ZodError);

		const fractional = validCatalogue();
		swatches(fractional)[0].price = 1.5;
		expect(() => parseCatalogue(fractional)).toThrow(ZodError);
	});

	it("rejects a non-boolean hidden", () => {
		const catalogue = validCatalogue();
		swatches(catalogue)[0].hidden = "yes";
		expect(() => parseCatalogue(catalogue)).toThrow(ZodError);
	});

	it("rejects a swatch id that is also an item id", () => {
		const catalogue = validCatalogue();
		swatches(catalogue)[0].id = "uni:round";
		expect(() => parseCatalogue(catalogue)).toThrow(ZodError);
	});

	it("accepts the reset pattern on a hidden swatch", () => {
		const catalogue = validCatalogue();
		swatches(catalogue)[0].pattern = "none";
		swatches(catalogue)[0].hidden = true;

		expect(parseCatalogue(catalogue).palettes[0].swatches[0].pattern).toBe("none");
	});

	it("rejects an unknown swatch pattern", () => {
		const catalogue = validCatalogue();
		swatches(catalogue)[0].pattern = "checker";
		expect(() => parseCatalogue(catalogue)).toThrow(ZodError);
	});
});
