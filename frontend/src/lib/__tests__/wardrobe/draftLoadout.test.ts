// The wardrobe's draft-loadout helpers, kept pure: every call returns a fresh
// loadout and never mutates its input. A set clears the part slots it covers and
// an unequip only removes the slot it was asked to.

import { describe, expect, it } from "vitest";
import { setCatalogueForTest } from "$lib/avatar/catalogue";
import { blockedSlots, equip, isDirty, setColour, unequip } from "$lib/avatar/draftLoadout";
import { avatarFixture, fullLoadout } from "$lib/__tests__/avatar/avatarFixture";
import type { CatalogueItem } from "$lib/avatar/types";

const catalogue = avatarFixture();

function item(id: string): CatalogueItem {
	const found = catalogue.items.find((entry) => entry.id === id);
	if (!found) throw new Error(`no catalogue item ${id}`);
	return found;
}

describe("equip", () => {
	it("puts a part item into its slot and leaves the input untouched", () => {
		const before = fullLoadout();
		const next = equip(before, catalogue, item("uni:hat_float"));

		expect(next.slots.head_top).toEqual({ item: "uni:hat_float", colours: [before.skin] });
		expect(before.slots.head_top?.item).toBe("uni:hat_cap");
	});

	it("tints the first region of a new part with the skin and leaves a set uncolored", () => {
		const before = fullLoadout();
		const part = equip(before, catalogue, item("uni:mask_bandit"));
		const set = equip(before, catalogue, item("uni:set_suit"));

		expect(part.slots.mask?.colours).toEqual([before.skin]);
		expect(set.slots.set?.colours).toEqual([]);
	});

	it("clears the part slots a worn set covers", () => {
		const before = fullLoadout();
		const next = equip(before, catalogue, item("uni:set_suit"));

		expect(next.slots.set).toEqual({ item: "uni:set_suit", colours: [] });
		expect(next.slots.head_top).toBeUndefined();
		expect(next.slots.mask).toBeUndefined();
		expect(next.slots.bodywear).toBeUndefined();
		expect(next.slots.eyes?.item).toBe("uni:eyes_dot");
		expect(before.slots.head_top?.item).toBe("uni:hat_cap");
	});
});

describe("blockedSlots", () => {
	it("names the part slots a worn set covers", () => {
		const withSet = equip(fullLoadout(), catalogue, item("uni:set_suit"));
		expect(blockedSlots(withSet, catalogue)).toEqual(new Set(["head_top", "mask", "bodywear"]));
	});

	it("is empty without a set", () => {
		expect(blockedSlots(fullLoadout(), catalogue)).toEqual(new Set());
	});
});

describe("unequip", () => {
	it("removes only the requested slot", () => {
		const before = fullLoadout();
		const next = unequip(before, "mask");

		expect(next.slots.mask).toBeUndefined();
		expect(next.slots.head_top?.item).toBe("uni:hat_cap");
		expect(before.slots.mask).toBeDefined();
	});

	it("restores nothing a set removed", () => {
		const withSet = equip(fullLoadout(), catalogue, item("uni:set_suit"));
		const next = unequip(withSet, "head_top");

		expect(next.slots.head_top).toBeUndefined();
		expect(next.slots.set?.item).toBe("uni:set_suit");
		expect(next.slots.eyes?.item).toBe("uni:eyes_dot");
	});
});

describe("setColour", () => {
	it("changes exactly one region and leaves the input untouched", () => {
		const before = fullLoadout();
		const next = setColour(before, "motif", 1, "basic:blue");

		expect(next.slots.motif?.colours[1]).toBe("basic:blue");
		expect(next.slots.motif?.colours[0]).toBe("basic:green");
		expect(before.slots.motif?.colours[1]).toBe("basic:red");
	});

	it("sets the skin swatch for the shape slot", () => {
		const next = setColour(fullLoadout(), "shape", 0, "basic:blue");
		expect(next.skin).toBe("basic:blue");
	});

	it("never stores an empty swatch id for a skipped region", () => {
		setCatalogueForTest(avatarFixture());
		const before = fullLoadout({
			slots: { motif: { item: "uni:motif_patches", colours: [] } }
		});
		const next = setColour(before, "motif", 1, "basic:green");

		expect(next.slots.motif?.colours).toEqual(["basic:red", "basic:green"]);
	});

	it("fills a skipped region with the item's swatch default", () => {
		const catalogue = avatarFixture();
		const patches = catalogue.items.find((entry) => entry.id === "uni:motif_patches");
		if (patches) patches.default_colors = ["basic:blue", "basic:green"];
		setCatalogueForTest(catalogue);
		const before = fullLoadout({
			slots: { motif: { item: "uni:motif_patches", colours: [] } }
		});
		const next = setColour(before, "motif", 1, "basic:red");

		expect(next.slots.motif?.colours).toEqual(["basic:blue", "basic:red"]);
	});
});

describe("isDirty", () => {
	it("is false for structurally equal loadouts", () => {
		expect(isDirty(fullLoadout(), fullLoadout())).toBe(false);
	});

	it("is true after an equipped item changes", () => {
		const changed = equip(fullLoadout(), catalogue, item("uni:hat_float"));
		expect(isDirty(changed, fullLoadout())).toBe(true);
	});
});
