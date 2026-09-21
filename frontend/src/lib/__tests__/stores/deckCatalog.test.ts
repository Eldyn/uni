import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { storeDeckCatalog, type DeckInfo } from "$stores/deckCatalog.svelte";

const fetchMock = vi.fn();
global.fetch = fetchMock as unknown as typeof fetch;

const classicDeck: DeckInfo = {
	id: "classic",
	name: "Classic",
	namespace: "vanilla",
	mods: []
};

function okResponse(body: unknown): Response {
	return { ok: true, json: async () => body } as unknown as Response;
}

describe("storeDeckCatalog.fetchDecks", () => {
	beforeEach(() => {
		fetchMock.mockReset();
		storeDeckCatalog.decks = [];
		storeDeckCatalog.isLoading = false;
		storeDeckCatalog.error = false;
	});

	afterEach(() => {
		vi.restoreAllMocks();
	});

	it("populates decks on a successful response", async () => {
		fetchMock.mockResolvedValueOnce(okResponse({ decks: [classicDeck] }));

		await storeDeckCatalog.fetchDecks();

		expect(storeDeckCatalog.decks).toEqual([classicDeck]);
		expect(storeDeckCatalog.error).toBe(false);
		expect(fetchMock).toHaveBeenCalledWith("/api/decks", {
			credentials: "include",
			headers: { "Content-Type": "application/json" }
		});
	});

	it("defaults to an empty list when the response omits decks", async () => {
		fetchMock.mockResolvedValueOnce(okResponse({}));

		await storeDeckCatalog.fetchDecks();

		expect(storeDeckCatalog.decks).toEqual([]);
		expect(storeDeckCatalog.error).toBe(false);
	});

	it("sets error and leaves the list untouched when the response is not ok", async () => {
		storeDeckCatalog.decks = [classicDeck];
		fetchMock.mockResolvedValueOnce({ ok: false, json: async () => ({}) } as unknown as Response);

		await storeDeckCatalog.fetchDecks(true);

		expect(storeDeckCatalog.error).toBe(true);
		expect(storeDeckCatalog.decks).toEqual([classicDeck]);
	});

	it("sets error and does not throw on a network failure", async () => {
		const errorSpy = vi.spyOn(console, "error").mockImplementation(() => {});
		fetchMock.mockRejectedValueOnce(new Error("network down"));

		await expect(storeDeckCatalog.fetchDecks()).resolves.toBeUndefined();

		expect(storeDeckCatalog.error).toBe(true);
		expect(errorSpy).toHaveBeenCalled();
	});

	it("skips the request when a catalog is already loaded", async () => {
		storeDeckCatalog.decks = [classicDeck];

		await storeDeckCatalog.fetchDecks();

		expect(fetchMock).not.toHaveBeenCalled();
	});

	it("refetches when forced", async () => {
		storeDeckCatalog.decks = [classicDeck];
		fetchMock.mockResolvedValueOnce(okResponse({ decks: [] }));

		await storeDeckCatalog.fetchDecks(true);

		expect(fetchMock).toHaveBeenCalledTimes(1);
	});
});
