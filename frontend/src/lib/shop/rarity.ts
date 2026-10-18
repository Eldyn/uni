/**
 * @file rarity.ts
 * @brief Rarity colours and paraglide label keys for the shop and wardrobe.
 */

import type { Rarity } from "$lib/avatar/types";

/** The border and badge colour for each rarity. */
export const RARITY_COLOURS: Record<Rarity, string> = {
	common: "#9ca3af",
	uncommon: "#4ade80",
	rare: "#60a5fa",
	epic: "#ffcc00"
};

/** The paraglide message key that names a rarity. */
export type RarityLabelKey =
	"shop_rarity_common" | "shop_rarity_uncommon" | "shop_rarity_rare" | "shop_rarity_epic";

/** The paraglide message key for a rarity label. */
export function rarityLabelKey(rarity: Rarity): RarityLabelKey {
	return `shop_rarity_${rarity}` as RarityLabelKey;
}
