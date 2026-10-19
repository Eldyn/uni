/**
 * @file draftLoadout.ts
 * @brief Pure edits on a wardrobe draft loadout.
 * The wardrobe edits a copy of the saved loadout; every helper here returns a
 * new loadout and never mutates the one it is given.
 */

import { defaultItemChoice } from "./previewLoadout";
import type { AvatarSlot, Catalogue, CatalogueItem, Loadout, PartSlot } from "./types";

/** The part slots a full set covers: equipping a set clears them. */
export const SET_BLOCKED_PART_SLOTS: readonly PartSlot[] = ["head_top", "mask", "bodywear"];

/** A slot that can be emptied: every slot except the always-on shape. */
export type UnequipSlot = Exclude<AvatarSlot, "shape">;

/** A deep copy of a loadout, so a draft cannot reach the saved value. */
export function cloneLoadout(loadout: Loadout): Loadout {
	const slots: Loadout["slots"] = {};
	for (const [slot, choice] of Object.entries(loadout.slots)) {
		if (!choice) continue;
		slots[slot as UnequipSlot] = { item: choice.item, colours: [...choice.colours] };
	}
	return { shape: loadout.shape, skin: loadout.skin, slots };
}

/**
 * @brief Wears one item on a copy of the loadout.
 * A shape replaces the shape; a set also clears the part slots it covers; a
 * part or motif lands in its own slot with its default colors.
 */
export function equip(loadout: Loadout, catalogue: Catalogue, item: CatalogueItem): Loadout {
	const entry = catalogue.items.find((candidate) => candidate.id === item.id) ?? item;
	const next = cloneLoadout(loadout);

	if (entry.slot === "shape") {
		next.shape = entry.id;
		return next;
	}

	if (entry.slot === "set") {
		for (const slot of SET_BLOCKED_PART_SLOTS) delete next.slots[slot];
	}
	// INFO: the wardrobe lists owned items only, so the default colors apply.
	next.slots[entry.slot] = defaultItemChoice(entry, next.skin, true);
	return next;
}

/** Takes one slot off a copy of the loadout; nothing it did not hold comes back. */
export function unequip(loadout: Loadout, slot: UnequipSlot): Loadout {
	const next = cloneLoadout(loadout);
	delete next.slots[slot];
	return next;
}

/**
 * @brief Tints one region of one slot on a copy of the loadout.
 * For the shape slot region 0 is the skin swatch. A missing slot is left alone.
 */
export function setColour(
	loadout: Loadout,
	slot: AvatarSlot,
	region: number,
	swatchId: string
): Loadout {
	const next = cloneLoadout(loadout);

	if (slot === "shape") {
		next.skin = swatchId;
		return next;
	}

	const choice = next.slots[slot];
	if (!choice) return next;

	const colours = [...choice.colours];
	// INFO: leave a gap for any regions before this one that were never set.
	while (colours.length < region) colours.push("");
	colours[region] = swatchId;
	next.slots[slot] = { item: choice.item, colours };
	return next;
}

/** The part slots the worn set covers, or an empty set when none is worn. */
export function blockedSlots(loadout: Loadout, catalogue: Catalogue): Set<PartSlot> {
	const blocked = new Set<PartSlot>();
	const choice = loadout.slots.set;
	if (!choice) return blocked;

	const worn = catalogue.items.find((candidate) => candidate.id === choice.item);
	if (worn && worn.slot !== "set") return blocked;

	for (const slot of SET_BLOCKED_PART_SLOTS) blocked.add(slot);
	return blocked;
}

/** Whether two loadouts render differently: a structural, order-free compare. */
export function isDirty(a: Loadout, b: Loadout): boolean {
	if (a.shape !== b.shape || a.skin !== b.skin) return true;

	const keys = new Set([...Object.keys(a.slots), ...Object.keys(b.slots)]);
	for (const key of keys) {
		const slot = key as UnequipSlot;
		const left = a.slots[slot];
		const right = b.slots[slot];

		if (!left || !right) {
			if (left !== right) return true;
			continue;
		}
		if (left.item !== right.item) return true;
		if (left.colours.length !== right.colours.length) return true;
		for (let index = 0; index < left.colours.length; index++) {
			if (left.colours[index] !== right.colours[index]) return true;
		}
	}
	return false;
}
