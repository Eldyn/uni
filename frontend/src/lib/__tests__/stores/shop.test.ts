// The shop store's wishlist and popular surface: optimistic toggles that roll
// back on failure, and no request at all for guests.

import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import type {
	BuyResult,
	PopularItem,
	PopularResult,
	ShopBackend,
	WishlistResult
} from "$lib/shop/shopBackend";
import { setShopBackend, storeShop } from "$lib/stores/shop.svelte";
import { storeAuth } from "$lib/stores/auth.svelte";

type TestBackend = ShopBackend & {
	fetchMine: () => Promise<void>;
	wishlist: () => Promise<WishlistResult>;
	toggleWishlist: (item: string) => Promise<WishlistResult>;
	popular: (days?: number) => Promise<PopularResult>;
};

function makeBackend(
	options: {
		wishlist?: string[];
		popular?: PopularItem[];
		toggleWishlist?: (item: string) => Promise<WishlistResult>;
	} = {}
): TestBackend {
	let items = [...(options.wishlist ?? [])];
	const toggle = vi.fn(async (item: string): Promise<WishlistResult> => {
		items = items.includes(item) ? items.filter((entry) => entry !== item) : [...items, item];
		return { ok: true, items };
	});

	return {
		balance: 0,
		owned: new Set<string>(),
		buy: vi.fn(async (): Promise<BuyResult> => ({ ok: true, balance: 0 })),
		wishlist: vi.fn(async (): Promise<WishlistResult> => ({ ok: true, items })),
		toggleWishlist: options.toggleWishlist ?? toggle,
		popular: vi.fn(async (): Promise<PopularResult> => ({
			ok: true,
			items: options.popular ?? []
		})),
		fetchMine: vi.fn(async () => {})
	};
}

const POPULAR: PopularItem[] = [
	{ item: "uni:hat_cap", purchases: 9, wishlists: 4 },
	{ item: "uni:round", purchases: 3, wishlists: 0 }
];

describe("storeShop wishlist and popular", () => {
	beforeEach(async () => {
		storeAuth.isGuest = false;
		storeAuth.isLoggedIn = true;
		setShopBackend(makeBackend());
		await storeShop.fetchWishlist();
		await storeShop.fetchPopular();
	});

	afterEach(() => {
		storeAuth.isGuest = false;
	});

	it("adds an item optimistically before the request resolves", async () => {
		let resolve!: (value: WishlistResult) => void;
		const pending = new Promise<WishlistResult>((done) => {
			resolve = done;
		});
		setShopBackend(makeBackend({ toggleWishlist: () => pending }));

		const toggling = storeShop.toggleWishlist("uni:hat_cap");

		expect(storeShop.wishlist.has("uni:hat_cap")).toBe(true);
		resolve({ ok: true, items: ["uni:hat_cap"] });
		await toggling;
		expect(storeShop.wishlist.has("uni:hat_cap")).toBe(true);
	});

	it("rolls back an optimistic add when the request fails", async () => {
		setShopBackend(makeBackend({ toggleWishlist: async () => ({ ok: false, code: "network" }) }));

		await storeShop.toggleWishlist("uni:hat_cap");

		expect(storeShop.wishlist.has("uni:hat_cap")).toBe(false);
	});

	it("rolls back an optimistic remove when the request fails", async () => {
		setShopBackend(
			makeBackend({
				wishlist: ["uni:hat_cap"],
				toggleWishlist: async () => ({ ok: false, code: "internal" })
			})
		);
		await storeShop.fetchWishlist();

		await storeShop.toggleWishlist("uni:hat_cap");

		expect(storeShop.wishlist.has("uni:hat_cap")).toBe(true);
	});

	it("takes the server's list after a successful toggle", async () => {
		setShopBackend(makeBackend());

		await storeShop.toggleWishlist("uni:hat_cap");

		expect([...storeShop.wishlist]).toEqual(["uni:hat_cap"]);
		await storeShop.toggleWishlist("uni:hat_cap");
		expect(storeShop.wishlist.size).toBe(0);
	});

	it("parses popular items and counts wishlists per item", async () => {
		setShopBackend(makeBackend({ popular: POPULAR }));

		await storeShop.fetchPopular(30);

		expect(storeShop.popular).toEqual(POPULAR);
		expect(storeShop.wishlistCount("uni:hat_cap")).toBe(4);
		expect(storeShop.wishlistCount("uni:unknown")).toBe(0);
	});

	it("gives a guest an empty wishlist and sends no request", async () => {
		storeAuth.isGuest = true;
		const backend = makeBackend();
		setShopBackend(backend);

		await storeShop.toggleWishlist("uni:hat_cap");
		await storeShop.fetchWishlist();

		expect(backend.toggleWishlist).not.toHaveBeenCalled();
		expect(backend.wishlist).not.toHaveBeenCalled();
		expect(storeShop.wishlist.size).toBe(0);
	});
});
