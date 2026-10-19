import { describe, it, expect, beforeEach } from "vitest";
import { isBuyableNow } from "$lib/shop/shopBackend";
import { ShopMock, shopMock } from "$lib/stores/shopMock.svelte";
import type { Catalogue, CatalogueItem } from "$lib/avatar/types";

const NOW = Date.parse("2026-10-19T12:00:00Z");

function makeItem(overrides: Partial<CatalogueItem> = {}): CatalogueItem {
	return {
		id: "uni:test",
		slot: "eyes",
		name: "Test",
		asset: { kind: "strip", path: "test.png" },
		regions: 1,
		rarity: "common",
		price: 100,
		...overrides
	};
}

function makeCatalogue(items: CatalogueItem[]): Catalogue {
	return { version: 1, shape_sockets: {}, palettes: [], items, featured: [] };
}

describe("isBuyableNow", () => {
	it("is false for a starter item", () => {
		expect(isBuyableNow(makeItem({ starter: true }), NOW)).toBe(false);
	});

	it("is false for a hidden item", () => {
		expect(isBuyableNow(makeItem({ hidden: true }), NOW)).toBe(false);
	});

	it("is false for an unlisted item", () => {
		expect(isBuyableNow(makeItem({ unlisted: true }), NOW)).toBe(false);
	});

	it("is false before available_from", () => {
		expect(isBuyableNow(makeItem({ available_from: "2026-10-20T00:00:00Z" }), NOW)).toBe(false);
	});

	it("is false after available_until", () => {
		expect(isBuyableNow(makeItem({ available_until: "2026-10-18T00:00:00Z" }), NOW)).toBe(false);
	});

	it("is true inside the date window", () => {
		const item = makeItem({
			available_from: "2026-10-18T00:00:00Z",
			available_until: "2026-10-20T00:00:00Z"
		});
		expect(isBuyableNow(item, NOW)).toBe(true);
	});

	it("is true for a plain listed item", () => {
		expect(isBuyableNow(makeItem(), NOW)).toBe(true);
	});
});

describe("ShopMock", () => {
	const catalogue = makeCatalogue([
		makeItem({ id: "uni:starter", slot: "shape", price: 0, starter: true }),
		makeItem({ id: "uni:exact", price: 1500 }),
		makeItem({ id: "uni:pricey", price: 2000 }),
		makeItem({ id: "uni:hidden", price: 100, hidden: true })
	]);

	let mock: ShopMock;

	beforeEach(() => {
		mock = new ShopMock(catalogue);
	});

	it("starts with a balance of 1500", () => {
		expect(mock.balance).toBe(1500);
	});

	it("starts owning the starters only", () => {
		expect(mock.owned.has("uni:starter")).toBe(true);
		expect(mock.owned.has("uni:exact")).toBe(false);
	});

	it("buying exactly at the price leaves a balance of 0", async () => {
		const result = await mock.buy("uni:exact");
		expect(result).toEqual({ ok: true, balance: 0 });
		expect(mock.balance).toBe(0);
	});

	it("one short gives insufficient_cash and changes nothing", async () => {
		const result = await mock.buy("uni:pricey");
		expect(result).toEqual({ ok: false, reason: "insufficient_cash" });
		expect(mock.balance).toBe(1500);
		expect(mock.owned.has("uni:pricey")).toBe(false);
	});

	it("a second buy of an owned item gives already_owned", async () => {
		await mock.buy("uni:exact");
		const result = await mock.buy("uni:exact");
		expect(result).toEqual({ ok: false, reason: "already_owned" });
		expect(mock.balance).toBe(0);
	});

	it("gives already_owned for a starter", async () => {
		const result = await mock.buy("uni:starter");
		expect(result).toEqual({ ok: false, reason: "already_owned" });
	});

	it("gives unknown_item for an id the catalogue does not list", async () => {
		const result = await mock.buy("uni:nope");
		expect(result).toEqual({ ok: false, reason: "unknown_item" });
	});

	it("gives not_available for a hidden item", async () => {
		const result = await mock.buy("uni:hidden");
		expect(result).toEqual({ ok: false, reason: "not_available" });
		expect(mock.balance).toBe(1500);
	});

	it("gives guest for a guest account", async () => {
		const guest = new ShopMock(catalogue, { guest: true });
		const result = await guest.buy("uni:exact");
		expect(result).toEqual({ ok: false, reason: "guest" });
		expect(guest.balance).toBe(1500);
	});

	it("deducts once for two rapid buys of the same item", async () => {
		const results = await Promise.all([mock.buy("uni:exact"), mock.buy("uni:exact")]);
		expect(mock.balance).toBe(0);
		expect(results.filter((result) => result.ok)).toHaveLength(1);
	});
});

describe("shopMock singleton", () => {
	it("is a ShopMock with the launch balance and starters owned", () => {
		expect(shopMock).toBeInstanceOf(ShopMock);
		expect(shopMock.balance).toBe(1500);
		expect(shopMock.owned.has("uni:round")).toBe(true);
		expect(shopMock.owned.has("uni:eyes_dot")).toBe(true);
	});
});
