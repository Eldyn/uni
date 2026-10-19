/**
 * @file shop.svelte.ts
 * @brief The economy backend the Shop and Wardrobe screens read.
 * Points at the server-backed store by default; tests swap in the mock through
 * `setShopBackend`, so no screen imports a concrete backend.
 */

import { shopServer } from "$lib/stores/shopServer.svelte";
import type { BuyResult, ShopBackend } from "$lib/shop/shopBackend";

/** A backend plus the optional account-side surface the server store offers. */
export type ShopStore = ShopBackend & {
	/** Fetches the account economy; absent on a backend with nothing to fetch. */
	fetchMine?: () => Promise<void>;
	/** True when the account's session was rejected and login is needed. */
	needsLogin?: boolean;
};

/** The backend the screens read, the server store unless a test swaps it. */
let backend: ShopStore = shopServer;

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
	buy(itemId: string): Promise<BuyResult> {
		return backend.buy(itemId);
	},
	fetchMine(): Promise<void> | undefined {
		return backend.fetchMine?.();
	}
};

/** Points the screens at another backend. Used by tests to inject the mock. */
export function setShopBackend(next: ShopStore): void {
	backend = next;
}
