/**
 * @file rarity.ts
 * @brief Rarity colours and paraglide label keys for the shop and wardrobe.
 */

import { RARITIES } from "$lib/avatar/types";
import type { Palette, Rarity } from "$lib/avatar/types";
import { HOME_BACKGROUND_HINT_HEX, hexToCss } from "$lib/shaders/homePalette";

/** The border and badge colour for each rarity. */
export const RARITY_COLOURS: Record<Rarity, string> = {
	basic: hexToCss(HOME_BACKGROUND_HINT_HEX),
	common: "#9ca3af",
	rare: "#4ade80",
	epic: "#60a5fa",
	special: "#c084fc"
};

/** The rarity label color. Basic is too dark to read as text. */
export function rarityLabelColour(rarity: Rarity): string {
	return rarity === "basic" ? "var(--text)" : RARITY_COLOURS[rarity];
}

/** Sort order: basic, common, rare, epic, special. */
export function compareRarity(a: Rarity, b: Rarity): number {
	return RARITIES.indexOf(a) - RARITIES.indexOf(b);
}

/** A starter palette is basic; any other takes its highest swatch rarity. */
export function paletteRarity(palette: Palette): Rarity {
	if (palette.starter === true) return "basic";
	return palette.swatches.reduce<Rarity>((top, swatch) => {
		const rarity = swatch.rarity ?? "common";
		return compareRarity(rarity, top) > 0 ? rarity : top;
	}, "common");
}

/** The paraglide message key that names a rarity. */
export type RarityLabelKey = `shop_rarity_${Rarity}`;

/** The paraglide message key for a rarity label. */
export function rarityLabelKey(rarity: Rarity): RarityLabelKey {
	return `shop_rarity_${rarity}` as RarityLabelKey;
}
