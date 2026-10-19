import { afterEach, beforeEach, describe, expect, it } from "vitest";
import { setCatalogueForTest } from "$lib/avatar/catalogue";
import { defaultItemChoice, itemPreviewLoadout } from "$lib/avatar/previewLoadout";
import { resolveLayers } from "$lib/avatar/slotRules";
import { hexToRgb } from "$lib/avatar/tint";
import type { CatalogueItem, Loadout } from "$lib/avatar/types";
import { avatarFixture } from "./avatarFixture";

const FIRST_SWATCH = "basic:red";
const SKIN = "basic:green";

function makeItem(overrides: Partial<CatalogueItem>): CatalogueItem {
	return {
		id: "uni:test",
		slot: "eyes",
		name: "Test",
		asset: { kind: "strip", path: "eyes/uni_test.png" },
		regions: 1,
		rarity: "common",
		price: 100,
		...overrides
	};
}

const viewer: Loadout = {
	shape: "uni:round",
	skin: SKIN,
	slots: { eyes: { item: "uni:eyes_dot", colours: ["basic:green"] } }
};

beforeEach(() => setCatalogueForTest(avatarFixture()));
afterEach(() => setCatalogueForTest(avatarFixture()));

describe("defaultItemChoice", () => {
	it("gives region 1 the skin and the other regions the first swatch when owned", () => {
		const choice = defaultItemChoice(makeItem({ regions: 3 }), SKIN, true);

		expect(choice.colours).toEqual([SKIN, FIRST_SWATCH, FIRST_SWATCH]);
	});

	it("gives every region the first swatch when the item is not owned", () => {
		const choice = defaultItemChoice(makeItem({ regions: 2 }), SKIN, false);

		expect(choice.colours).toEqual([FIRST_SWATCH, FIRST_SWATCH]);
	});

	it("skips region 1 for a motif, because the skin already tints it", () => {
		const choice = defaultItemChoice(makeItem({ slot: "motif", regions: 3 }), SKIN, true);

		expect(choice.colours).toEqual([FIRST_SWATCH, FIRST_SWATCH]);
	});

	it("gives a no-region item an empty color list", () => {
		expect(defaultItemChoice(makeItem({ slot: "set", regions: 0 }), SKIN, true).colours).toEqual(
			[]
		);
	});
});

describe("itemPreviewLoadout", () => {
	it("wears an owned part on the viewer's shape and skin", () => {
		const preview = itemPreviewLoadout(makeItem({ slot: "head_top" }), viewer, true);

		expect(preview.shape).toBe("uni:round");
		expect(preview.skin).toBe(SKIN);
		expect(preview.slots).toEqual({ head_top: { item: "uni:test", colours: [SKIN] } });
	});

	it("uses the first swatch for the skin and the part when not owned", () => {
		const preview = itemPreviewLoadout(makeItem({ slot: "head_top" }), viewer, false);

		expect(preview.skin).toBe(FIRST_SWATCH);
		expect(preview.slots.head_top?.colours).toEqual([FIRST_SWATCH]);
	});

	it("previews a shape as the bare body with the default face", () => {
		const preview = itemPreviewLoadout(makeItem({ slot: "shape", id: "uni:tall" }), viewer, true);

		expect(preview.shape).toBe("uni:tall");
		expect(preview.skin).toBe(SKIN);
		expect(Object.keys(preview.slots).sort()).toEqual(["eyes", "mouth"]);
	});

	it("does not change the viewer's loadout", () => {
		const before = JSON.stringify(viewer);
		itemPreviewLoadout(makeItem({ slot: "mask" }), viewer, true);

		expect(JSON.stringify(viewer)).toBe(before);
	});

	it("resolves to a tinted layer, not an uncolored one", () => {
		const preview = itemPreviewLoadout(
			makeItem({ id: "uni:eyes_wide", slot: "eyes" }),
			viewer,
			true
		);
		const layer = resolveLayers(preview).find((entry) => entry.slot === "eyes");

		expect(layer?.colours[0]).toEqual(hexToRgb("#4aa85a"));
	});
});
