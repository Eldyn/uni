import { describe, it, expect, vi, beforeEach } from "vitest";

vi.mock("$lib/stores/analytics.svelte", () => ({ storeAnalytics: { track: vi.fn() } }));
vi.mock("$lib/stores/ws.svelte", () => ({ ws: { onOpen: vi.fn() } }));
vi.mock("$lib/stores/game.svelte", () => ({ storeGame: { state: null, returnToLobby: vi.fn() } }));
vi.mock("$lib/stores/lobby.svelte", () => ({
	storeLobby: { isInLobby: false, leave: vi.fn() }
}));
vi.mock("$lib/stores/auth.svelte", () => ({ storeAuth: { isLoggedIn: true, isGuest: false } }));

beforeEach(() => {
	vi.resetModules();
	window.history.replaceState(null, "", "/");
});

describe("settings modal", () => {
	it("opens and closes without changing the current screen", async () => {
		const { storeNavigation } = await import("$lib/stores/navigation.svelte");

		storeNavigation.goto("lobbies");
		storeNavigation.openSettings();

		expect(storeNavigation.isSettingsOpen).toBe(true);
		expect(storeNavigation.current).toBe("lobbies");

		storeNavigation.closeSettings();

		expect(storeNavigation.isSettingsOpen).toBe(false);
		expect(storeNavigation.current).toBe("lobbies");
	});

	it("is closeable via a back gesture, restoring the underlying screen", async () => {
		const { storeNavigation } = await import("$lib/stores/navigation.svelte");

		storeNavigation.goto("lobbies");
		storeNavigation.openSettings();
		window.history.back();
		// jsdom dispatches popstate off a macrotask, not the current one — a
		// single setTimeout(0) can still race it, so wait two ticks.
		await new Promise((r) => setTimeout(r, 0));
		await new Promise((r) => setTimeout(r, 0));

		expect(storeNavigation.isSettingsOpen).toBe(false);
	});
});
