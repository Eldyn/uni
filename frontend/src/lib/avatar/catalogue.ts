import catalogueJson from "virtual:avatar-catalogue";
import { parseCatalogue } from "./catalogueSchema";
import type { AvatarSlot, Catalogue, CatalogueItem, Palette, Swatch } from "./types";

let cache: Catalogue | undefined;

/** Parse the bundled catalogue once and cache it. */
export function getCatalogue(): Catalogue {
	if (!cache) cache = parseCatalogue(catalogueJson);
	return cache;
}

/** Install a catalogue for tests and reset the cache. */
export function setCatalogueForTest(catalogue: Catalogue): void {
	cache = catalogue;
}

/** The item with this id, or `undefined` when it is not in the catalogue. */
export function getItem(id: string): CatalogueItem | undefined {
	return getCatalogue().items.find((item) => item.id === id);
}

/** The first starter item in the slot, or `undefined` when the slot is empty. */
export function slotDefault(slot: AvatarSlot): CatalogueItem | undefined {
	return getCatalogue().items.find((item) => item.slot === slot && item.starter === true);
}

/** The item when it exists in this slot, otherwise the slot default. */
export function resolveItem(id: string, slot: AvatarSlot): CatalogueItem | undefined {
	const item = getItem(id);
	return item && item.slot === slot ? item : slotDefault(slot);
}

/** Whether the item is a starter item owned by every account. */
export function isStarter(id: string): boolean {
	return getItem(id)?.starter === true;
}

/** Whether the item is visible right now: not unlisted, within its window. */
export function isAvailable(item: CatalogueItem, now: Date): boolean {
	if (item.unlisted) return false;
	if (item.available_from && now < new Date(item.available_from)) return false;
	if (item.available_until && now >= new Date(item.available_until)) return false;
	return true;
}

/** Purchasable items: not hidden, not unlisted, available, not a starter. */
export function shopItems(now: Date): CatalogueItem[] {
	return getCatalogue().items.filter(
		(item) => !item.hidden && item.starter !== true && isAvailable(item, now)
	);
}

/** The featured ids that are currently shop-visible, in featured order. */
export function featuredItems(now: Date): CatalogueItem[] {
	const visible = new Set(shopItems(now).map((item) => item.id));
	return getCatalogue()
		.featured.filter((id) => visible.has(id))
		.map((id) => getItem(id) as CatalogueItem);
}

/** The swatch and its palette, or `undefined` when no palette holds it. */
export function findSwatch(id: string): { swatch: Swatch; palette: Palette } | undefined {
	for (const palette of getCatalogue().palettes) {
		const swatch = palette.swatches.find((entry) => entry.id === id);
		if (swatch) return { swatch, palette };
	}
	return undefined;
}

/** The swatch with this id, or `undefined` when no palette holds it. */
export function getSwatch(id: string): Swatch | undefined {
	return findSwatch(id)?.swatch;
}

/** Whether the swatch belongs to a starter palette. */
export function isStarterSwatch(id: string): boolean {
	for (const palette of getCatalogue().palettes) {
		if (palette.swatches.some((swatch) => swatch.id === id)) return palette.starter === true;
	}
	return false;
}
