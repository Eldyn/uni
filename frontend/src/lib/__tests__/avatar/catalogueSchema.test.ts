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
				body_centre: [0, 40]
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
		delete sockets["uni:round"].body_centre;
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
