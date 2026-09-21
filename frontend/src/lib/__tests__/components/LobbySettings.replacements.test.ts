import { describe, it, expect, vi, beforeEach } from "vitest";
import { render, screen, fireEvent, within } from "@testing-library/svelte";

const { updateSettings, decks } = vi.hoisted(() => ({
	updateSettings: vi.fn(),
	decks: [] as Array<{ id: string; name: string; namespace: string; mods: string[] }>
}));

const auth = vi.hoisted(() => ({ username: "eldyn" }));

vi.mock("$stores/auth.svelte", () => ({ storeAuth: auth }));
vi.mock("$stores/catalog.svelte", () => ({ storeCatalog: { ensureLoaded: vi.fn(), rules: [] } }));
vi.mock("$stores/deckCatalog.svelte", () => ({
	storeDeckCatalog: { fetchDecks: vi.fn(), decks }
}));
vi.mock("$stores/lobby.svelte", () => ({
	storeLobby: {
		current: {
			host: "eldyn",
			settings: {
				is_public: true,
				turn_time_limit_ms: 15000,
				allow_bot_replacement: false,
				allow_bot_takeover: false,
				starting_cards: 7,
				max_players: 4,
				quit_deletes_match: false,
				bot_mode: 0,
				bot_count: 1,
				active_mods: []
			}
		},
		updateSettings
	}
}));

import LobbySettings from "$components/lobby/LobbySettings.svelte";

describe("LobbySettings replacement bindings", () => {
	beforeEach(() => {
		vi.clearAllMocks();
		auth.username = "eldyn";
		decks.length = 0;
	});

	it("binds 'Allow Players to Replace Bots' to allow_bot_takeover", async () => {
		render(LobbySettings);
		const takeoverToggle = screen.getByRole("checkbox", {
			name: "Allow Players to Replace Bots"
		});
		expect(takeoverToggle).toBeInTheDocument();

		await fireEvent.click(takeoverToggle);
		expect(updateSettings).toHaveBeenCalledWith({ allow_bot_takeover: true });
	});

	it("binds 'Bot Takes Over Empty Seat' to allow_bot_replacement", async () => {
		render(LobbySettings);
		const replacementToggle = screen.getByRole("checkbox", {
			name: "Bot Takes Over Empty Seat"
		});
		expect(replacementToggle).toBeInTheDocument();

		await fireEvent.click(replacementToggle);
		expect(updateSettings).toHaveBeenCalledWith({ allow_bot_replacement: true });
	});

	it("renders 'Ranked Match' toggle defaulting to true and calls updateSettings on toggle", async () => {
		render(LobbySettings);
		const rankedToggle = screen.getByRole("checkbox", {
			name: "Ranked Match"
		});
		expect(rankedToggle).toBeInTheDocument();
		expect(rankedToggle).toBeChecked();

		await fireEvent.click(rankedToggle);
		expect(updateSettings).toHaveBeenCalledWith({ ranked: false });
	});
});

describe("LobbySettings deck picker", () => {
	beforeEach(() => {
		vi.clearAllMocks();
		auth.username = "eldyn";
		decks.length = 0;
	});

	it("lists every catalogue deck plus a freestyle option", () => {
		decks.push(
			{ id: "vanilla:classic", name: "Classic", namespace: "vanilla", mods: [] },
			{ id: "chaos:wild", name: "Wild", namespace: "chaos", mods: ["chaos"] }
		);
		render(LobbySettings);

		const select = screen.getByRole("combobox", { name: "Deck" });
		const options = within(select).getAllByRole("option");
		expect(options.map((option) => option.textContent?.trim())).toEqual([
			"Freestyle",
			"Classic",
			"Wild"
		]);
	});

	it("commits the selected catalogue deck id", async () => {
		decks.push({ id: "vanilla:classic", name: "Classic", namespace: "vanilla", mods: [] });
		render(LobbySettings);

		const select = screen.getByRole("combobox", { name: "Deck" });
		select.selectedIndex = 1;
		await fireEvent.change(select);

		expect(updateSettings).toHaveBeenCalledWith({ deck_id: "vanilla:classic" });
	});

	it("commits an empty deck_id when freestyle is selected", async () => {
		decks.push({ id: "vanilla:classic", name: "Classic", namespace: "vanilla", mods: [] });
		render(LobbySettings);

		const select = screen.getByRole("combobox", { name: "Deck" });
		select.selectedIndex = 0;
		await fireEvent.change(select);

		expect(updateSettings).toHaveBeenCalledWith({ deck_id: "" });
	});

	it("disables the deck selector for non-hosts", () => {
		auth.username = "intruder";
		render(LobbySettings);

		expect(screen.getByRole("combobox", { name: "Deck" })).toBeDisabled();
	});
});
