/**
 * @file shopBackend.ts
 * @brief The economy interface the Shop screen talks to, plus its helpers.
 * Today the mock store implements it; the real client can replace it later.
 */

import { isAvailable } from "$lib/avatar/catalogue";
import type { CatalogueItem, Palette, Swatch } from "$lib/avatar/types";

/** Why a purchase failed. `network` means the request never produced a usable answer. */
export type BuyFailure =
	"insufficient_cash" | "already_owned" | "not_available" | "unknown_item" | "guest" | "network";

/** The outcome of a purchase: the new balance, or a reason it did not happen. */
export type BuyResult = { ok: true; balance: number } | { ok: false; reason: BuyFailure };

/** What the Shop screen needs from an economy backend. */
export interface ShopBackend {
	readonly balance: number;
	readonly owned: ReadonlySet<string>;
	buy(itemId: string): Promise<BuyResult>;
}

/**
 * Whether the shop lists the item as buyable at `nowMs`: it is not a starter,
 * not hidden, not unlisted, and inside its availability window.
 */
export function isBuyableNow(item: CatalogueItem, nowMs: number): boolean {
	if (item.starter === true || item.hidden) return false;
	return isAvailable(item, new Date(nowMs));
}

/** A swatch with the palette that holds it. */
export interface SwatchEntry {
	swatch: Swatch;
	palette: Palette;
}

/**
 * Whether the shop sells the swatch alone now: the palette is not a starter
 * palette, the swatch is not hidden or unlisted, and it has a price.
 */
export function isSwatchBuyable(entry: SwatchEntry): boolean {
	if (entry.palette.starter === true) return false;
	if (entry.swatch.hidden === true || entry.swatch.unlisted === true) return false;
	return entry.swatch.price !== undefined;
}
