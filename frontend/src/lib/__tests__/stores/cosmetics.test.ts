// The cosmetics store: it reads the equipped slots from GET /cosmetics/me and
// equips through PUT /cosmetics/me, keeping the last good state on failure.

import { beforeEach, describe, expect, it, vi } from "vitest";
import { storeCosmetics } from "$stores/cosmetics.svelte";
import type { CosmeticSlots } from "$lib/shop/shopBackend";

global.fetch = vi.fn();

const SLOTS: CosmeticSlots = {
	card_face: "uni:face_neon",
	card_back: null,
	playmat: "uni:mat_felt"
};

function jsonResponse(body: unknown, status = 200): Response {
	return new Response(JSON.stringify(body), { status });
}

describe("storeCosmetics", () => {
	beforeEach(() => {
		vi.resetAllMocks();
		storeCosmetics.reset();
	});

	it("fetch fills the slots from GET /cosmetics/me", async () => {
		vi.mocked(fetch).mockResolvedValueOnce(jsonResponse({ slots: SLOTS }));

		await storeCosmetics.fetch();

		expect(fetch).toHaveBeenCalledWith("/cosmetics/me", {
			method: "GET",
			credentials: "include",
			headers: { "Content-Type": "application/json" },
			body: undefined
		});
		expect(storeCosmetics.slots).toEqual(SLOTS);
		expect(storeCosmetics.status).toBe("ready");
	});

	it("fetch keeps the previous slots and sets error on a bad body", async () => {
		vi.mocked(fetch)
			.mockResolvedValueOnce(jsonResponse({ slots: SLOTS }))
			.mockResolvedValueOnce(jsonResponse({ slots: "none" }));

		await storeCosmetics.fetch();
		await storeCosmetics.fetch();

		expect(storeCosmetics.slots).toEqual(SLOTS);
		expect(storeCosmetics.status).toBe("error");
	});

	it("equip applies the slot optimistically before the server answers", async () => {
		vi.mocked(fetch).mockResolvedValueOnce(
			jsonResponse({ slots: { ...SLOTS, card_face: "uni:face_neon" } })
		);

		const pending = storeCosmetics.equip("card_face", "uni:face_neon");

		expect(storeCosmetics.slots.card_face).toBe("uni:face_neon");

		const result = await pending;

		expect(result).toEqual({ ok: true });
		expect(fetch).toHaveBeenCalledWith("/cosmetics/me", {
			method: "PUT",
			credentials: "include",
			headers: { "Content-Type": "application/json" },
			body: JSON.stringify({ slot: "card_face", item: "uni:face_neon" })
		});
		expect(storeCosmetics.slots).toEqual({ ...SLOTS, card_face: "uni:face_neon" });
	});

	it("equip rolls the slot back and returns the server's code on failure", async () => {
		vi.mocked(fetch)
			.mockResolvedValueOnce(jsonResponse({ slots: SLOTS }))
			.mockResolvedValueOnce(jsonResponse({ error: "not_owned" }, 400));

		await storeCosmetics.fetch();

		const result = await storeCosmetics.equip("card_face", "uni:face_missing");

		expect(result).toEqual({ ok: false, code: "not_owned" });
		expect(storeCosmetics.slots.card_face).toBe("uni:face_neon");
	});

	it("equip rolls back on a dropped connection", async () => {
		vi.mocked(fetch)
			.mockResolvedValueOnce(jsonResponse({ slots: SLOTS }))
			.mockRejectedValueOnce(new TypeError("connection lost"));

		await storeCosmetics.fetch();

		const result = await storeCosmetics.equip("playmat", "uni:mat_other");

		expect(result).toEqual({ ok: false, code: "network" });
		expect(storeCosmetics.slots.playmat).toBe("uni:mat_felt");
	});

	it("reset clears the previous account's slots", async () => {
		vi.mocked(fetch).mockResolvedValueOnce(jsonResponse({ slots: SLOTS }));
		await storeCosmetics.fetch();

		storeCosmetics.reset();

		expect(storeCosmetics.slots).toEqual({ card_face: null, card_back: null, playmat: null });
		expect(storeCosmetics.status).toBe("idle");
	});
});
