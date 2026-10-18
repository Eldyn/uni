/**
 * @file shopMock.svelte.ts
 * @brief Client-side mock economy for the Shop prototype.
 * Follows the economy spec's purchase rules so the real backend can replace it
 * behind the same `ShopBackend` interface.
 */

import { getCatalogue, isAvailable } from "$lib/avatar/catalogue";
import type { Catalogue, CatalogueItem } from "$lib/avatar/types";
import type { BuyResult, ShopBackend } from "$lib/shop/shopBackend";

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
	#guest: boolean;

	/** The account's cash balance. */
	balance = $state(DEFAULT_BALANCE);

	#owned = $state<Set<string>>(new Set());

	/**
	 * @param catalogue The catalogue to read items and starters from.
	 * @param options Balance and guest overrides for tests.
	 */
	constructor(catalogue: Catalogue = getCatalogue(), options: ShopMockOptions = {}) {
		this.#items = catalogue.items;
		this.#guest = options.guest ?? false;
		this.balance = options.balance ?? DEFAULT_BALANCE;
		for (const item of catalogue.items) {
			if (item.starter === true) this.#owned.add(item.id);
		}
	}

	/** The ids the account owns, starters included. */
	get owned(): ReadonlySet<string> {
		return this.#owned;
	}

	/**
	 * @brief Attempts a purchase following the spec 2 transaction rules.
	 * Rejects unknown items, guests, unavailable items, owned items and items
	 * the balance cannot cover; otherwise deducts the price and grants the item.
	 * The body runs synchronously, so two rapid calls cannot both succeed.
	 */
	async buy(itemId: string): Promise<BuyResult> {
		const item = this.#items.find((entry) => entry.id === itemId);
		if (!item) return { ok: false, reason: "unknown_item" };
		if (this.#guest) return { ok: false, reason: "guest" };
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
}

/** Singleton instance exported for use by the Svelte components. */
export const shopMock = new ShopMock();
