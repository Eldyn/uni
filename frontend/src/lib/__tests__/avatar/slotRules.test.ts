import { beforeEach, describe, expect, it } from "vitest";
import { setCatalogueForTest } from "$lib/avatar/catalogue";
import { ART_VERSION, hashWithVersion, loadoutHash, resolveLayers } from "$lib/avatar/slotRules";
import { hexToRgb } from "$lib/avatar/tint";
import { avatarFixture, fullLoadout, sixRegionItem } from "./avatarFixture";

function slotsOf(overrides: Parameters<typeof fullLoadout>[0] = {}) {
	return resolveLayers(fullLoadout(overrides)).map((layer) => layer.slot);
}

describe("resolveLayers", () => {
	beforeEach(() => {
		setCatalogueForTest(avatarFixture());
	});

	it("draw order is shape, motif, bodywear, mouth, eyes, mask, head_top", () => {
		expect(slotsOf()).toEqual(["shape", "motif", "bodywear", "mouth", "eyes", "mask", "head_top"]);
	});

	it("a set replaces the shape, the motif and all part slots", () => {
		expect(slotsOf({ slots: { set: { item: "uni:set_suit", colours: [] } } })).toEqual(["set"]);
	});

	it("with a set neither the shape nor the motif draws", () => {
		const layers = resolveLayers(
			fullLoadout({ slots: { set: { item: "uni:set_suit", colours: [] } } })
		);

		expect(layers.map((layer) => layer.slot)).toEqual(["set"]);
	});

	it("an unknown item id bakes the slot default", () => {
		const layers = resolveLayers(
			fullLoadout({ slots: { eyes: { item: "uni:missing", colours: [] } } })
		);

		expect(layers.find((layer) => layer.slot === "eyes")?.item.id).toBe("uni:eyes_dot");
	});

	it("an unknown item in an optional slot with no default is dropped", () => {
		expect(slotsOf({ slots: { head_top: { item: "uni:missing", colours: [] } } })).not.toContain(
			"head_top"
		);
	});

	it("motif region 0 takes the skin colour", () => {
		const motif = resolveLayers(fullLoadout()).find((layer) => layer.slot === "motif")!;

		expect(motif.colours[0]).toEqual(hexToRgb("#e84a4a"));
		expect(motif.colours[1]).toEqual(hexToRgb("#4aa85a"));
	});

	it("an unknown swatch id leaves the region without colour", () => {
		const layers = resolveLayers(
			fullLoadout({ slots: { eyes: { item: "uni:eyes_dot", colours: ["missing:swatch"] } } })
		);
		const eyes = layers.find((layer) => layer.slot === "eyes")!;

		expect(eyes.colours[0]).toBeUndefined();
	});

	it("a six-region motif keeps the skin in region 1 and five chosen colours", () => {
		const catalogue = avatarFixture();
		catalogue.items.push(sixRegionItem());
		setCatalogueForTest(catalogue);

		const layers = resolveLayers(
			fullLoadout({
				slots: {
					motif: {
						item: "uni:motif_rainbow",
						colours: ["basic:green", "basic:green", "basic:green", "basic:green", "basic:green"]
					}
				}
			})
		);

		const motif = layers.find((layer) => layer.slot === "motif");
		expect(motif?.colours).toHaveLength(6);
		expect(motif?.colours.every((colour) => colour !== undefined)).toBe(true);
	});
});

describe("loadoutHash", () => {
	beforeEach(() => {
		setCatalogueForTest(avatarFixture());
	});

	it("is equal for equal render results", () => {
		const explicit = fullLoadout({ slots: { eyes: { item: "uni:eyes_dot", colours: [] } } });
		const byDefault = fullLoadout({ slots: { eyes: { item: "uni:missing", colours: [] } } });

		expect(loadoutHash(explicit)).toBe(loadoutHash(byDefault));
	});

	it("ignores a part choice blocked by a set", () => {
		const base = fullLoadout({ slots: { set: { item: "uni:set_suit", colours: [] } } });
		const other = fullLoadout({
			slots: {
				set: { item: "uni:set_suit", colours: [] },
				eyes: { item: "uni:eyes_wide", colours: ["basic:green"] }
			}
		});

		expect(loadoutHash(base)).toBe(loadoutHash(other));
	});

	it("ignores colours beyond the item's region count", () => {
		const one = fullLoadout({ slots: { eyes: { item: "uni:eyes_dot", colours: ["basic:red"] } } });
		const two = fullLoadout({
			slots: { eyes: { item: "uni:eyes_dot", colours: ["basic:red", "basic:green"] } }
		});

		expect(loadoutHash(one)).toBe(loadoutHash(two));
	});

	it("changes with ART_VERSION", () => {
		const loadout = fullLoadout();

		expect(hashWithVersion(loadout, ART_VERSION + 1)).not.toBe(
			hashWithVersion(loadout, ART_VERSION)
		);
		expect(loadoutHash(loadout)).toBe(hashWithVersion(loadout, ART_VERSION));
	});

	it("changes when the extra mask files of an item change", () => {
		const loadout = fullLoadout({
			slots: { motif: { item: "uni:motif_rainbow", colours: ["basic:green"] } }
		});

		const withMask = avatarFixture();
		withMask.items.push(sixRegionItem());
		setCatalogueForTest(withMask);
		const hashWithMask = loadoutHash(loadout);

		const otherMask = avatarFixture();
		const item = sixRegionItem();
		item.asset = { kind: "strip", path: item.asset.path, masks: ["motif/other_b.png"] };
		otherMask.items.push(item);
		setCatalogueForTest(otherMask);

		expect(loadoutHash(loadout)).not.toBe(hashWithMask);
	});
});

describe("default colors", () => {
	function withDefaults(id: string, defaults: string[]) {
		const catalogue = avatarFixture();
		const item = catalogue.items.find((entry) => entry.id === id);
		if (!item) throw new Error(`no item ${id}`);
		item.default_colors = defaults;
		setCatalogueForTest(catalogue);
	}

	function eyesColours(colours: string[]) {
		const layers = resolveLayers(
			fullLoadout({ slots: { eyes: { item: "uni:eyes_dot", colours } } })
		);
		return layers.find((layer) => layer.slot === "eyes")?.colours;
	}

	it("an item with no chosen colors uses its hex default", () => {
		withDefaults("uni:eyes_dot", ["#102030"]);

		expect(eyesColours([])?.[0]).toEqual([16, 32, 48]);
	});

	it("a swatch id default resolves through the palette", () => {
		withDefaults("uni:eyes_dot", ["basic:green"]);

		expect(eyesColours([])?.[0]).toEqual(hexToRgb("#4aa85a"));
	});

	it("a chosen swatch wins over the default", () => {
		withDefaults("uni:eyes_dot", ["basic:green"]);

		expect(eyesColours(["basic:red"])?.[0]).toEqual(hexToRgb("#e84a4a"));
	});

	it("an empty chosen entry falls back to the default", () => {
		withDefaults("uni:eyes_dot", ["basic:green"]);

		expect(eyesColours([""])?.[0]).toEqual(hexToRgb("#4aa85a"));
	});

	it("an item with no defaults and no choice stays uncolored", () => {
		setCatalogueForTest(avatarFixture());

		expect(eyesColours([])?.[0]).toBeUndefined();
	});

	it("a motif default starts at region 2", () => {
		withDefaults("uni:motif_patches", ["basic:green", "basic:red"]);
		const layers = resolveLayers(
			fullLoadout({ slots: { motif: { item: "uni:motif_patches", colours: [] } } })
		);
		const motif = layers.find((layer) => layer.slot === "motif")?.colours;

		expect(motif?.[1]).toEqual(hexToRgb("#4aa85a"));
		expect(motif?.[2]).toEqual(hexToRgb("#e84a4a"));
	});

	it("the loadout hash changes with the tint mode", () => {
		const loadout = fullLoadout();
		setCatalogueForTest(avatarFixture());
		const before = loadoutHash(loadout);
		const catalogue = avatarFixture();
		const shape = catalogue.items.find((entry) => entry.id === "uni:round");
		if (shape) shape.tint = "multiply";
		setCatalogueForTest(catalogue);

		expect(loadoutHash(loadout)).not.toBe(before);
	});

	it("the loadout hash changes with the none tint mode", () => {
		const loadout = fullLoadout({ slots: { set: { item: "uni:set_suit", colours: [] } } });
		setCatalogueForTest(avatarFixture());
		const before = loadoutHash(loadout);
		const catalogue = avatarFixture();
		const set = catalogue.items.find((entry) => entry.id === "uni:set_suit");
		if (set) set.tint = "none";
		setCatalogueForTest(catalogue);

		expect(loadoutHash(loadout)).not.toBe(before);
	});

	it("ART_VERSION is 2", () => {
		expect(ART_VERSION).toBe(2);
	});
});
