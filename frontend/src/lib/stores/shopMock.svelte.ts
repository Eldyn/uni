/**
 * @file shopMock.svelte.ts
 * @brief Client-side mock economy for the Shop prototype.
 * Follows the economy spec's purchase rules so the real backend can replace it
 * behind the same `ShopBackend` interface.
 */

import { getCatalogue, isAvailable } from "$lib/avatar/catalogue";
import type { Catalogue, CatalogueItem, Palette } from "$lib/avatar/types";
import { isSwatchBuyable } from "$lib/shop/shopBackend";
import type { BuyResult, ShopBackend, SwatchEntry } from "$lib/shop/shopBackend";

/** The balance every fresh account starts with in the prototype. */
export const DEFAULT_BALANCE = 1500;

/** Optional construction overrides, used by tests. */
export interface ShopMockOptions {
	balance?: number;
	guest?: boolean;
}

/**
 * @class ShopMock
 * @brief In-memory balance and ownership with the spec 2 purchase rules.
 */
export class ShopMock implements ShopBackend {
	#items: readonly CatalogueItem[];
	#palettes: readonly Palette[];
	#guest: boolean;

	/** The account's cash balance. */
	balance = $state(DEFAULT_BALANCE);

	#owned = $state<Set<string>>(new Set());

	/**
	 * @param catalogue The catalogue to read items, palettes and starters from.
	 * @param options Balance and guest overrides for tests.
	 */
	constructor(catalogue: Catalogue = getCatalogue(), options: ShopMockOptions = {}) {
		this.#items = catalogue.items;
		this.#palettes = catalogue.palettes;
		this.#guest = options.guest ?? false;
		this.balance = options.balance ?? DEFAULT_BALANCE;
		for (const item of catalogue.items) {
			if (item.starter === true) this.#owned.add(item.id);
		}
		for (const palette of catalogue.palettes) {
			if (palette.starter !== true) continue;
			for (const swatch of palette.swatches) this.#owned.add(swatch.id);
		}
	}

	/** The ids the account owns, items and swatches, starters included. */
	get owned(): ReadonlySet<string> {
		return this.#owned;
	}

	/**
	 * @brief Attempts a purchase following the spec 2 transaction rules.
	 * An item and a swatch take the same path after the catalogue lookup.
	 */
	async buy(itemId: string): Promise<BuyResult> {
		const item = this.#items.find((entry) => entry.id === itemId);
		const swatch = this.#findSwatch(itemId);
		if (!item && !swatch) return { ok: false, reason: "unknown_item" };
		if (this.#guest) return { ok: false, reason: "guest" };

		if (!item && swatch) {
			if (this.#owned.has(itemId)) return { ok: false, reason: "already_owned" };
			if (!isSwatchBuyable(swatch)) return { ok: false, reason: "not_available" };
			const price = swatch.swatch.price ?? 0;
			if (this.balance < price) return { ok: false, reason: "insufficient_cash" };
			this.balance -= price;
			this.#owned.add(itemId);
			return { ok: true, balance: this.balance };
		}

		if (!item) return { ok: false, reason: "unknown_item" };
		if (item.hidden || !isAvailable(item, new Date())) {
			return { ok: false, reason: "not_available" };
		}
		if (item.starter === true || this.#owned.has(itemId)) {
			return { ok: false, reason: "already_owned" };
		}
		if (this.balance < item.price) return { ok: false, reason: "insufficient_cash" };

		this.balance -= item.price;
		this.#owned.add(itemId);
		return { ok: true, balance: this.balance };
	}

	#findSwatch(id: string): SwatchEntry | undefined {
		for (const palette of this.#palettes) {
			const swatch = palette.swatches.find((entry) => entry.id === id);
			if (swatch) return { swatch, palette };
		}
		return undefined;
	}
}

/** Singleton instance exported for use by the Svelte components. */
export const shopMock = new ShopMock();
