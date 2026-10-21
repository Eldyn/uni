// The server-backed shop store: it reads GET /economy/me and buys over
// POST /shop/buy, validating every response before trusting it.

import { beforeEach, describe, expect, it, vi } from "vitest";
import { ShopServer } from "$stores/shopServer.svelte";
import type { LedgerRow } from "$stores/shopServer.svelte";

global.fetch = vi.fn();

const UUID = /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/i;

const LEDGER: LedgerRow[] = [
	{ amount: 20, source: "match_base", ref: "match-1", created_at: 1_760_000_000 },
	{ amount: -100, source: "purchase", ref: "request-1", created_at: 1_760_000_100 }
];

function jsonResponse(body: unknown, status = 200): Response {
	return new Response(JSON.stringify(body), { status });
}

/** The body of the most recent fetch call, parsed as JSON. */
function sentBody(call: number): { item: string; request_id: string } {
	const init = vi.mocked(fetch).mock.calls[call]?.[1] as RequestInit | undefined;
	return JSON.parse(String(init?.body)) as { item: string; request_id: string };
}

describe("ShopServer", () => {
	let store: ShopServer;

	beforeEach(() => {
		vi.resetAllMocks();
		store = new ShopServer();
	});

	describe("fetchMine", () => {
		it("fills balance, owned and ledger from a good body", async () => {
			vi.mocked(fetch).mockResolvedValueOnce(
				jsonResponse({ balance: 1240, owned: ["uni:round", "uni:hat_cap"], ledger: LEDGER })
			);

			await store.fetchMine();

			expect(fetch).toHaveBeenCalledWith("/economy/me", {
				credentials: "include",
				headers: { "Content-Type": "application/json" }
			});
			expect(store.balance).toBe(1240);
			expect([...store.owned]).toEqual(["uni:round", "uni:hat_cap"]);
			expect(store.ledger).toEqual(LEDGER);
			expect(store.status).toBe("ready");
			expect(store.needsLogin).toBe(false);
		});

		it("sets status error and needsLogin on a 401", async () => {
			vi.mocked(fetch).mockResolvedValueOnce(new Response(null, { status: 401 }));

			await store.fetchMine();

			expect(store.status).toBe("error");
			expect(store.needsLogin).toBe(true);
		});

		it("leaves an empty balance and status ready for a guest 403", async () => {
			vi.mocked(fetch).mockResolvedValueOnce(jsonResponse({ error: "guest" }, 403));

			await store.fetchMine();

			expect(store.balance).toBe(0);
			expect(store.owned.size).toBe(0);
			expect(store.ledger).toEqual([]);
			expect(store.status).toBe("ready");
			expect(store.needsLogin).toBe(false);
		});

		it("sets status error on a malformed body", async () => {
			vi.mocked(fetch).mockResolvedValueOnce(
				jsonResponse({ balance: "lots", owned: "none", ledger: "none" })
			);

			await store.fetchMine();

			expect(store.status).toBe("error");
			expect(store.balance).toBe(0);
		});
	});

	describe("buy", () => {
		it("sends the item and a request id, then updates balance and owned on 200", async () => {
			vi.mocked(fetch).mockResolvedValueOnce(jsonResponse({ balance: 1400, item: "uni:hat_cap" }));

			const result = await store.buy("uni:hat_cap");

			expect(result).toEqual({ ok: true, balance: 1400 });
			expect(fetch).toHaveBeenCalledTimes(1);
			const [url, init] = vi.mocked(fetch).mock.calls[0];
			expect(url).toBe("/shop/buy");
			expect(init).toMatchObject({
				method: "POST",
				credentials: "include",
				headers: { "Content-Type": "application/json" }
			});
			const body = sentBody(0);
			expect(body.item).toBe("uni:hat_cap");
			expect(body.request_id).toMatch(UUID);
			expect(store.balance).toBe(1400);
			expect(store.owned.has("uni:hat_cap")).toBe(true);
		});

		it.each([
			[402, "insufficient_cash"],
			[409, "already_owned"],
			[410, "not_available"],
			[404, "unknown_item"],
			[403, "guest"]
		] as const)("maps a %i response to %s", async (status, reason) => {
			vi.mocked(fetch).mockResolvedValueOnce(jsonResponse({ error: reason }, status));

			const result = await store.buy("uni:hat_cap");

			expect(result).toEqual({ ok: false, reason });
			expect(store.balance).toBe(0);
			expect(store.owned.size).toBe(0);
		});

		it("returns network and changes nothing on a network error", async () => {
			vi.mocked(fetch).mockRejectedValueOnce(new TypeError("connection lost"));

			const result = await store.buy("uni:hat_cap");

			expect(result).toEqual({ ok: false, reason: "network" });
			expect(store.balance).toBe(0);
			expect(store.owned.size).toBe(0);
		});

		it("sends one request and one request id for two rapid buys of an item", async () => {
			vi.mocked(fetch).mockResolvedValueOnce(jsonResponse({ balance: 1400, item: "uni:hat_cap" }));

			const [first, second] = await Promise.all([
				store.buy("uni:hat_cap"),
				store.buy("uni:hat_cap")
			]);

			expect(fetch).toHaveBeenCalledTimes(1);
			expect(first).toEqual({ ok: true, balance: 1400 });
			expect(second).toEqual(first);
			expect(store.balance).toBe(1400);
		});

		it("reuses the request id when the same click is retried after a network error", async () => {
			vi.mocked(fetch)
				.mockRejectedValueOnce(new TypeError("connection lost"))
				.mockResolvedValueOnce(jsonResponse({ balance: 1400, item: "uni:hat_cap" }));

			await store.buy("uni:hat_cap");
			await store.buy("uni:hat_cap");

			expect(fetch).toHaveBeenCalledTimes(2);
			expect(sentBody(1).request_id).toBe(sentBody(0).request_id);
		});

		it("keeps a swatch id in owned and buys it by the same id", async () => {
			vi.mocked(fetch)
				.mockResolvedValueOnce(
					jsonResponse({ balance: 750, owned: ["basic:red", "uni:neon:volt"], ledger: [] })
				)
				.mockResolvedValueOnce(jsonResponse({ balance: 150, item: "uni:neon:volt" }));

			await store.fetchMine();
			expect(store.owned.has("uni:neon:volt")).toBe(true);

			const result = await store.buy("uni:neon:volt");
			expect(result).toEqual({ ok: true, balance: 150 });
			expect(sentBody(1).item).toBe("uni:neon:volt");
			expect(store.owned.has("uni:neon:volt")).toBe(true);
		});
	});

	describe("wishlist", () => {
		it("reads the items from GET /shop/wishlist", async () => {
			vi.mocked(fetch).mockResolvedValueOnce(jsonResponse({ items: ["uni:hat_cap", "uni:round"] }));

			const result = await store.wishlist();

			expect(result).toEqual({ ok: true, items: ["uni:hat_cap", "uni:round"] });
			expect(fetch).toHaveBeenCalledWith("/shop/wishlist", {
				method: "GET",
				credentials: "include",
				headers: { "Content-Type": "application/json" }
			});
		});

		it("adds with POST and the item body, then returns the updated list", async () => {
			vi.mocked(fetch).mockResolvedValueOnce(jsonResponse({ items: ["uni:hat_cap"] }));

			const result = await store.toggleWishlist("uni:hat_cap");

			expect(result).toEqual({ ok: true, items: ["uni:hat_cap"] });
			const [url, init] = vi.mocked(fetch).mock.calls[0];
			expect(url).toBe("/shop/wishlist");
			expect(init).toMatchObject({ method: "POST", credentials: "include" });
			expect(JSON.parse(String(init?.body))).toEqual({ item: "uni:hat_cap" });
		});

		it("removes with DELETE once the item is on the wishlist", async () => {
			vi.mocked(fetch)
				.mockResolvedValueOnce(jsonResponse({ items: ["uni:hat_cap"] }))
				.mockResolvedValueOnce(jsonResponse({ items: [] }));

			await store.wishlist();
			const result = await store.toggleWishlist("uni:hat_cap");

			expect(result).toEqual({ ok: true, items: [] });
			const [url, init] = vi.mocked(fetch).mock.calls[1];
			expect(url).toBe("/shop/wishlist");
			expect(init).toMatchObject({ method: "DELETE", credentials: "include" });
			expect(JSON.parse(String(init?.body))).toEqual({ item: "uni:hat_cap" });
		});

		it("returns the server's error code", async () => {
			vi.mocked(fetch).mockResolvedValueOnce(jsonResponse({ error: "guest" }, 403));

			expect(await store.toggleWishlist("uni:hat_cap")).toEqual({
				ok: false,
				code: "guest"
			});
		});

		it("returns network for a malformed body or a dropped connection", async () => {
			vi.mocked(fetch)
				.mockResolvedValueOnce(jsonResponse({ items: "none" }))
				.mockRejectedValueOnce(new TypeError("connection lost"));

			expect(await store.wishlist()).toEqual({ ok: false, code: "network" });
			expect(await store.wishlist()).toEqual({ ok: false, code: "network" });
		});
	});

	describe("popular", () => {
		it("requests seven days by default and parses the rows", async () => {
			const items = [
				{ item: "uni:hat_cap", purchases: 9, wishlists: 4 },
				{ item: "uni:round", purchases: 3, wishlists: 0 }
			];
			vi.mocked(fetch).mockResolvedValueOnce(jsonResponse({ days: 7, items }));

			const result = await store.popular();

			expect(result).toEqual({ ok: true, items });
			expect(fetch).toHaveBeenCalledWith("/shop/popular?days=7", {
				credentials: "include",
				headers: { "Content-Type": "application/json" }
			});
		});

		it("passes a custom window through the query string", async () => {
			vi.mocked(fetch).mockResolvedValueOnce(jsonResponse({ days: 30, items: [] }));

			await store.popular(30);

			expect(vi.mocked(fetch).mock.calls[0][0]).toBe("/shop/popular?days=30");
		});

		it("returns network for a malformed body", async () => {
			vi.mocked(fetch).mockResolvedValueOnce(
				jsonResponse({ days: 7, items: [{ item: 1, purchases: "many", wishlists: 0 }] })
			);

			expect(await store.popular()).toEqual({ ok: false, code: "network" });
		});
	});

	describe("cosmetics", () => {
		const slots = { card_face: "uni:face_neon", card_back: null, playmat: "uni:mat_felt" };

		it("reads the equipped slots from GET /cosmetics/me", async () => {
			vi.mocked(fetch).mockResolvedValueOnce(jsonResponse({ slots }));

			const result = await store.cosmetics();

			expect(result).toEqual({ ok: true, slots });
			expect(fetch).toHaveBeenCalledWith("/cosmetics/me", {
				method: "GET",
				credentials: "include",
				headers: { "Content-Type": "application/json" },
				body: undefined
			});
		});

		it("equips with PUT and returns the slots the server sent", async () => {
			vi.mocked(fetch).mockResolvedValueOnce(
				jsonResponse({ slots: { ...slots, card_face: "uni:face_other" } })
			);

			const result = await store.equipCosmetic("card_face", "uni:face_other");

			expect(result).toEqual({ ok: true, slots: { ...slots, card_face: "uni:face_other" } });
			const [url, init] = vi.mocked(fetch).mock.calls[0];
			expect(url).toBe("/cosmetics/me");
			expect(init).toMatchObject({
				method: "PUT",
				credentials: "include",
				headers: { "Content-Type": "application/json" }
			});
			expect(JSON.parse(String(init?.body))).toEqual({
				slot: "card_face",
				item: "uni:face_other"
			});
		});

		it("returns the server's error code and network for a malformed body", async () => {
			vi.mocked(fetch)
				.mockResolvedValueOnce(jsonResponse({ error: "not_owned" }, 400))
				.mockResolvedValueOnce(jsonResponse({ slots: "none" }));

			expect(await store.equipCosmetic("playmat", "uni:mat_other")).toEqual({
				ok: false,
				code: "not_owned"
			});
			expect(await store.cosmetics()).toEqual({ ok: false, code: "network" });
		});
	});
});
