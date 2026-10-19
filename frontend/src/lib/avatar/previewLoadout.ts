import { getCatalogue } from "./catalogue";
import { defaultLoadout } from "./defaultLoadout";
import type { CatalogueItem, Loadout, SlotChoice } from "./types";

/** The first swatch of the first palette; empty when the catalogue has none. */
function firstSwatchId(): string {
	return getCatalogue().palettes[0]?.swatches[0]?.id ?? "";
}

/**
 * The default colors of an item: an owned item takes the viewer's skin for its
 * first color region and the first swatch for the rest; an item that is not
 * owned takes the first swatch for every region. An item with `default_colors`
 * takes its swatch defaults first. INFO: a motif tints region 1 from the skin
 * by itself, so its list starts at region 2.
 */
export function defaultItemChoice(
	item: CatalogueItem,
	skinSwatchId: string,
	owned: boolean
): SlotChoice {
	const first = firstSwatchId();
	const count = item.slot === "motif" ? Math.max(0, item.regions - 1) : item.regions;
	const colours: string[] = [];
	for (let index = 0; index < count; index++) {
		const configured = item.default_colors?.[index];
		if (configured !== undefined) {
			// INFO: a hex default has no swatch id; stop and let the render use it.
			if (swatchDefault(item, index) === undefined) break;
			colours.push(configured);
			continue;
		}
		const bodyRegion = index === 0 && item.slot !== "motif";
		colours.push(owned && bodyRegion ? skinSwatchId : first);
	}
	return { item: item.id, colours };
}

/** A default entry that is a palette swatch id (a "#rrggbb" hex is not one). */
function swatchDefault(item: CatalogueItem | undefined, index: number): string | undefined {
	const entry = item?.default_colors?.[index];
	return entry && !entry.startsWith("#") ? entry : undefined;
}

/**
 * A valid swatch id for a skipped color slot: the item's swatch default, else
 * the first swatch. INFO: a loadout never stores "", the server rejects it.
 */
export function fillerSwatchId(item: CatalogueItem | undefined, index: number): string {
	return swatchDefault(item, index) ?? firstSwatchId();
}

/**
 * A loadout that shows one item with default colors. A shape previews as the
 * bare body with the default face; any other item is worn on the viewer's
 * shape. The viewer's loadout is never changed.
 */
export function itemPreviewLoadout(item: CatalogueItem, viewer: Loadout, owned: boolean): Loadout {
	const skin = owned ? viewer.skin : firstSwatchId();

	if (item.slot === "shape") {
		return { shape: item.id, skin, slots: defaultLoadout().slots };
	}

	return {
		shape: viewer.shape,
		skin,
		slots: { [item.slot]: defaultItemChoice(item, viewer.skin, owned) }
	};
}
