/**
 * @file shop.svelte.ts
 * @brief The economy backend the Shop and Wardrobe screens read.
 * Points at the server-backed store by default; tests swap in the mock through
 * `setShopBackend`, so no screen imports a concrete backend.
 */

import { shopServer } from "$lib/stores/shopServer.svelte";
import { storeAuth } from "$lib/stores/auth.svelte";
import type { BuyResult, PopularItem, ShopBackend } from "$lib/shop/shopBackend";

/** A backend plus the optional account-side surface the server store offers. */
type Backend = ShopBackend & {
	/** Fetches the account economy; absent on a backend with nothing to fetch. */
	fetchMine?: () => Promise<void>;
	/** True when the account's session was rejected and login is needed. */
	needsLogin?: boolean;
};

/** The economy surface the screens read, with the wishlist folded in. */
export type ShopStore = {
	readonly balance: number;
	readonly owned: ReadonlySet<string>;
	readonly needsLogin: boolean;
	/** Ids the account wants; always empty for a guest. */
	readonly wishlist: ReadonlySet<string>;
	/** The 7-day popular shelf, empty until fetched. */
	readonly popular: readonly PopularItem[];
	buy(itemId: string): Promise<BuyResult>;
	/** Adds or removes the item optimistically, rolling back on failure. */
	toggleWishlist(itemId: string): Promise<void>;
	/** How many accounts want the item, 0 when it is not on the popular shelf. */
	wishlistCount(itemId: string): number;
	fetchMine(): Promise<void> | undefined;
	fetchWishlist(): Promise<void>;
	fetchPopular(days?: number): Promise<void>;
};

/** The backend the screens read, the server store unless a test swaps it. */
let backend: Backend = shopServer;

/** The wishlist the server last sent, updated optimistically by a toggle. */
let wishlist = $state<Set<string>>(new Set());

/** The popular shelf rows, empty until `fetchPopular` succeeds. */
let popular = $state<PopularItem[]>([]);

const EMPTY_WISHLIST: ReadonlySet<string> = new Set();

/**
 * The active economy. Getters keep reads tied to the backend's own state, so a
 * screen re-renders whenever the account's balance or ownership changes.
 */
export const storeShop: ShopStore = {
	get balance(): number {
		return backend.balance;
	},
	get owned(): ReadonlySet<string> {
		return backend.owned;
	},
	get needsLogin(): boolean {
		return backend.needsLogin === true;
	},
	get wishlist(): ReadonlySet<string> {
		return storeAuth.isGuest ? EMPTY_WISHLIST : wishlist;
	},
	get popular(): readonly PopularItem[] {
		return popular;
	},
	buy(itemId: string): Promise<BuyResult> {
		return backend.buy(itemId);
	},
	async toggleWishlist(itemId: string): Promise<void> {
		if (storeAuth.isGuest) return;

		const before = new Set(wishlist);
		const next = new Set(wishlist);
		if (next.has(itemId)) next.delete(itemId);
		else next.add(itemId);
		wishlist = next;

		try {
			const result = await backend.toggleWishlist?.(itemId);
			wishlist = result?.ok === true ? new Set(result.items) : before;
		} catch {
			wishlist = before;
		}
	},
	wishlistCount(itemId: string): number {
		return popular.find((entry) => entry.item === itemId)?.wishlists ?? 0;
	},
	fetchMine(): Promise<void> | undefined {
		return backend.fetchMine?.();
	},
	async fetchWishlist(): Promise<void> {
		if (storeAuth.isGuest) {
			wishlist = new Set();
			return;
		}

		try {
			const result = await backend.wishlist?.();
			if (result?.ok === true) wishlist = new Set(result.items);
		} catch {
			// Keep the previous list; the screen still renders it.
		}
	},
	async fetchPopular(days = 7): Promise<void> {
		if (storeAuth.isGuest) {
			popular = [];
			return;
		}

		try {
			const result = await backend.popular?.(days);
			if (result?.ok === true) popular = result.items;
		} catch {
			// Keep the previous shelf; the screen still renders it.
		}
	}
};

/** Points the screens at another backend. Used by tests to inject the mock. */
export function setShopBackend(next: Backend): void {
	backend = next;
}
