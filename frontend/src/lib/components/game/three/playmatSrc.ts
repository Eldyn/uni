/**
 * @file playmatSrc.ts
 * @brief Resolves the table playmat's desktop and mobile sheet URLs for the
 * equipped `playmat` cosmetic.
 *
 * A `playmat` catalogue asset is the desktop sheet; its mobile counterpart
 * sits beside it with `_mobile` before the extension. With no equipped (or an
 * unknown) item, the base sheets are used. Both sheets keep the same felt
 * metrics, so only the URLs change here.
 */

import { getItem } from "$lib/avatar/catalogue";
import { assetUrl } from "$lib/utils/assetUrl";

const ASSETS = "/assets";
const BASE_DESKTOP = "/assets/playmat.png";
const BASE_MOBILE = "/assets/mobile_playmat.png";

/** The catalogue asset path when `itemId` is an equipped playmat. */
function playmatAsset(itemId: string | null): string | null {
	if (!itemId) return null;

	const item = getItem(itemId);
	return item?.slot === "playmat" ? item.asset.path : null;
}

/** Inserts `_mobile` before the path's extension, e.g. `dusk.png` -> `dusk_mobile.png`. */
function mobileSheet(path: string): string {
	const dot = path.lastIndexOf(".");
	return dot === -1 ? `${path}_mobile` : `${path.slice(0, dot)}_mobile${path.slice(dot)}`;
}

/**
 * @brief The desktop and mobile playmat sheet URLs for the equipped item.
 * @param skinItemId The equipped `playmat` catalogue item, if any.
 */
export function playmatUrls(skinItemId: string | null): { desktop: string; mobile: string } {
	const path = playmatAsset(skinItemId);
	if (!path) return { desktop: BASE_DESKTOP, mobile: BASE_MOBILE };

	return {
		desktop: assetUrl(`${ASSETS}/${path}`),
		mobile: assetUrl(`${ASSETS}/${mobileSheet(path)}`)
	};
}
