import { describe, it, expect, vi, beforeEach } from "vitest";
import { render, screen, fireEvent } from "@testing-library/svelte";

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
				active_mods: [],
				mode: "standard",
				race_percent: 50
			}
		},
		updateSettings
	}
}));

import { storeLobby } from "$stores/lobby.svelte";
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

	it("lists every catalogue deck plus a freestyle option", async () => {
		decks.push(
			{ id: "vanilla:classic", name: "Classic", namespace: "vanilla", mods: [] },
			{ id: "chaos:wild", name: "Wild", namespace: "chaos", mods: ["chaos"] }
		);
		render(LobbySettings);

		await fireEvent.click(screen.getByRole("button", { name: /Deck/ }));
		const options = screen.getAllByRole("option");
		expect(
			options.map((option) => option.querySelector(".enum-option__label")?.textContent?.trim())
		).toEqual(["Freestyle", "Classic", "Wild"]);
	});

	it("commits the selected catalogue deck id", async () => {
		decks.push({ id: "vanilla:classic", name: "Classic", namespace: "vanilla", mods: [] });
		render(LobbySettings);

		await fireEvent.click(screen.getByRole("button", { name: /Deck/ }));
		await fireEvent.click(screen.getByRole("option", { name: /Classic/ }));

		expect(updateSettings).toHaveBeenCalledWith({ deck_id: "vanilla:classic" });
	});

	it("commits an empty deck_id when freestyle is selected", async () => {
		decks.push({ id: "vanilla:classic", name: "Classic", namespace: "vanilla", mods: [] });
		render(LobbySettings);

		await fireEvent.click(screen.getByRole("button", { name: /Deck/ }));
		await fireEvent.click(screen.getByRole("option", { name: /Freestyle/ }));

		expect(updateSettings).toHaveBeenCalledWith({ deck_id: "" });
	});

	it("disables the deck selector for non-hosts", () => {
		auth.username = "intruder";
		render(LobbySettings);

		expect(screen.getByRole("button", { name: /Deck/ })).toBeDisabled();
	});
});

describe("LobbySettings race percent", () => {
	const settings = () => (storeLobby as any).current.settings;

	beforeEach(() => {
		vi.clearAllMocks();
		auth.username = "eldyn";
		settings().mode = "standard";
		settings().race_percent = 50;
		settings().max_players = 4;
	});

	it("hides the percent control outside race mode", () => {
		render(LobbySettings);
		expect(document.getElementById("race-percent")).toBeNull();
	});

	it("shows the percent control and resolved count in race mode", () => {
		settings().mode = "race";
		render(LobbySettings);
		expect(document.getElementById("race-percent")).not.toBeNull();
		expect(screen.getByText("50% = 2 of 4 players win")).toBeInTheDocument();
	});

	it("clamps the resolved count to leave at least one loser", () => {
		settings().mode = "race";
		settings().race_percent = 95;
		render(LobbySettings);
		expect(screen.getByText("95% = 3 of 4 players win")).toBeInTheDocument();
	});

	it("resolves at least one finisher at the minimum percent", () => {
		settings().mode = "race";
		settings().race_percent = 5;
		settings().max_players = 16;
		render(LobbySettings);
		expect(screen.getByText("5% = 1 of 16 players win")).toBeInTheDocument();
	});

	it("steps the percent by 5 and commits race_percent", async () => {
		settings().mode = "race";
		render(LobbySettings);
		await fireEvent.click(screen.getByRole("button", { name: "Players who win +" }));
		expect(updateSettings).toHaveBeenCalledWith({ race_percent: 55 });
		await fireEvent.click(screen.getByRole("button", { name: "Players who win −" }));
		expect(updateSettings).toHaveBeenLastCalledWith({ race_percent: 50 });
	});

	it("disables the percent control for non-hosts", () => {
		settings().mode = "race";
		auth.username = "someone-else";
		render(LobbySettings);
		expect(document.getElementById("race-percent")).toBeDisabled();
		expect(screen.getByRole("button", { name: "Players who win +" })).toBeDisabled();
	});
});
