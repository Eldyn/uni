/**
 * @file skinPaths.ts
 * @brief Resolves card face and card back URLs for the equipped cosmetics.
 *
 * A `card_face` catalogue asset is a folder of per-value PNGs; a `card_back`
 * asset is the PNG itself. With no equipped (or an unknown) item, the base
 * card art is used.
 */

import { getItem } from "$lib/avatar/catalogue";
import { assetUrl } from "$lib/utils/assetUrl";

const BASE_CARDS = "/assets/cards";
const ASSETS = "/assets";

/** The catalogue asset path when `itemId` is equipped in `slot`. */
function skinAsset(
	itemId: string | null | undefined,
	slot: "card_face" | "card_back"
): string | null {
	if (!itemId) return null;

	const item = getItem(itemId);
	return item?.slot === slot ? item.asset.path : null;
}

/**
 * @brief URL of the card face for `value`, skinned by `faceItemId`.
 * @param value The card value, e.g. `"7"` or `"+2"`.
 * @param faceItemId The equipped `card_face` catalogue item, if any.
 */
export function cardFaceUrl(value: string, faceItemId: string | null | undefined): string {
	const root = skinAsset(faceItemId, "card_face");
	return assetUrl(root ? `${ASSETS}/${root}/${value}.png` : `${BASE_CARDS}/${value}.png`);
}

/**
 * @brief URL of the card back, skinned by `backItemId`.
 * @param backItemId The equipped `card_back` catalogue item, if any.
 */
export function cardBackUrl(backItemId: string | null | undefined): string {
	const path = skinAsset(backItemId, "card_back");
	return assetUrl(path ? `${ASSETS}/${path}` : `${BASE_CARDS}/back.png`);
}
