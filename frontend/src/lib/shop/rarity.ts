/**
 * @file rarity.ts
 * @brief Rarity colours and paraglide label keys for the shop and wardrobe.
 */

import type { Rarity } from "$lib/avatar/types";
import { HOME_BACKGROUND_HINT_HEX, hexToCss } from "$lib/shaders/homePalette";

/** The border and badge colour for each rarity. */
export const RARITY_COLOURS: Record<Rarity, string> = {
	basic: hexToCss(HOME_BACKGROUND_HINT_HEX),
	common: "#9ca3af",
	rare: "#60a5fa",
	epic: "#ffcc00",
	special: "#c084fc"
};

/** The paraglide message key that names a rarity. */
export type RarityLabelKey = `shop_rarity_${Rarity}`;

/** The paraglide message key for a rarity label. */
export function rarityLabelKey(rarity: Rarity): RarityLabelKey {
	return `shop_rarity_${rarity}` as RarityLabelKey;
}
