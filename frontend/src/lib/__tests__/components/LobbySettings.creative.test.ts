import { describe, it, expect, vi, beforeEach } from "vitest";
import { render, screen, fireEvent } from "@testing-library/svelte";

const { updateSettings, settings } = vi.hoisted(() => ({
	updateSettings: vi.fn(),
	settings: {
		is_public: false,
		ranked: true,
		creative: false,
		scene: "",
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
}));

vi.mock("$stores/auth.svelte", () => ({
	storeAuth: { username: "eldyn", isLoggedIn: true, isGuest: false }
}));
vi.mock("$stores/catalog.svelte", () => ({ storeCatalog: { ensureLoaded: vi.fn(), rules: [] } }));
vi.mock("$stores/deckCatalog.svelte", () => ({
	storeDeckCatalog: { fetchDecks: vi.fn(), decks: [] }
}));
vi.mock("$stores/lobby.svelte", () => ({
	storeLobby: { current: { host: "eldyn", settings }, updateSettings }
}));
vi.mock("$lib/stores/analytics.svelte", () => ({ storeAnalytics: { track: vi.fn() } }));
vi.mock("$lib/paraglide/runtime.js", () => ({
	locales: ["en"],
	getLocale: () => "en",
	setLocale: vi.fn()
}));

import LobbySettings from "$components/lobby/LobbySettings.svelte";

describe("LobbySettings creative bindings", () => {
	beforeEach(() => {
		vi.clearAllMocks();
		settings.creative = false;
		settings.scene = "";
	});

	it("commits the creative toggle", async () => {
		render(LobbySettings);
		const toggle = screen.getByRole("checkbox", { name: "Creative director mode" });
		await fireEvent.click(toggle);
		expect(updateSettings).toHaveBeenCalledWith({ creative: true });
	});

	it("shows the scene input only in creative mode and commits it", async () => {
		render(LobbySettings);
		expect(screen.queryByPlaceholderText("sample_opening")).toBeNull();
	});
	it("commits the scene name", async () => {
		settings.creative = true;
		render(LobbySettings);
		const input = screen.getByPlaceholderText("sample_opening");
		await fireEvent.change(input, { target: { value: "opening" } });
		expect(updateSettings).toHaveBeenCalledWith({ scene: "opening" });
	});
});
