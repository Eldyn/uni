import { describe, it, expect, vi } from "vitest";
import { render, screen, fireEvent } from "@testing-library/svelte";

const { updateSettings } = vi.hoisted(() => ({
	updateSettings: vi.fn()
}));

vi.mock("$stores/auth.svelte", () => ({ storeAuth: { username: "eldyn" } }));
vi.mock("$stores/catalog.svelte", () => ({ storeCatalog: { ensureLoaded: vi.fn(), rules: [] } }));
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
