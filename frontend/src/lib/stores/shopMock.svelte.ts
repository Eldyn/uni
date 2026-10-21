/**
 * @file shopMock.svelte.ts
 * @brief Client-side mock economy for the Shop prototype.
 * Follows the economy spec's purchase rules so the real backend can replace it
 * behind the same `ShopBackend` interface.
 */

import { getCatalogue, isAvailable } from "$lib/avatar/catalogue";
import type { Catalogue, CatalogueItem, Palette } from "$lib/avatar/types";
import { isSwatchBuyable } from "$lib/shop/shopBackend";
import type {
	BuyResult,
	CosmeticSlot,
	CosmeticSlots,
	CosmeticsResult,
	PopularItem,
	PopularResult,
	ShopBackend,
	SwatchEntry,
	WishlistResult
} from "$lib/shop/shopBackend";

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

	#wishlist = $state<Set<string>>(new Set());

	#slots = $state<CosmeticSlots>({ card_face: null, card_back: null, playmat: null });

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

	/** @brief The wishlisted item ids, oldest first. */
	async wishlist(): Promise<WishlistResult> {
		if (this.#guest) return { ok: false, code: "guest" };
		return { ok: true, items: [...this.#wishlist] };
	}

	/**
	 * @brief Adds the item when missing, removes it otherwise.
	 * Follows the server's rules: guests are refused, unknown and hidden items
	 * are rejected.
	 */
	async toggleWishlist(itemId: string): Promise<WishlistResult> {
		if (this.#guest) return { ok: false, code: "guest" };

		const item = this.#items.find((entry) => entry.id === itemId);
		if (!item) return { ok: false, code: "unknown_item" };
		if (item.hidden) return { ok: false, code: "not_available" };

		if (this.#wishlist.has(itemId)) this.#wishlist.delete(itemId);
		else this.#wishlist.add(itemId);
		return { ok: true, items: [...this.#wishlist] };
	}

	/** @brief The listed catalogue items, with how many accounts want each. */
	async popular(_days = 7): Promise<PopularResult> {
		if (this.#guest) return { ok: false, code: "guest" };

		const items: PopularItem[] = this.#items
			.filter((entry) => !entry.hidden)
			.slice(0, 12)
			.map((entry) => ({
				item: entry.id,
				purchases: 0,
				wishlists: this.#wishlist.has(entry.id) ? 1 : 0
			}));
		return { ok: true, items };
	}

	/** @brief The equipped cosmetic slots, all empty until one is equipped. */
	async cosmetics(): Promise<CosmeticsResult> {
		if (this.#guest) return { ok: false, code: "guest" };
		return { ok: true, slots: this.#slots };
	}

	/**
	 * @brief Equips `item` in `slot`, following the server's guest rule.
	 */
	async equipCosmetic(slot: CosmeticSlot, item: string): Promise<CosmeticsResult> {
		if (this.#guest) return { ok: false, code: "guest" };
		this.#slots = { ...this.#slots, [slot]: item };
		return { ok: true, slots: this.#slots };
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
