import { describe, it, expect, vi, beforeEach } from "vitest";

vi.mock("$lib/stores/analytics.svelte", () => ({ storeAnalytics: { track: vi.fn() } }));
vi.mock("$lib/stores/ws.svelte", () => ({ ws: { onOpen: vi.fn() } }));
vi.mock("$lib/stores/game.svelte", () => ({ storeGame: { state: null, returnToLobby: vi.fn() } }));

const lobbyState = { current: null as unknown };
vi.mock("$lib/stores/lobby.svelte", () => ({
	storeLobby: {
		get isInLobby() {
			return lobbyState.current !== null;
		},
		leave: vi.fn()
	}
}));

vi.mock("$lib/stores/auth.svelte", () => ({
	storeAuth: { isLoggedIn: false, isGuest: false }
}));

beforeEach(() => {
	lobbyState.current = null;
	vi.resetModules();
	window.localStorage.clear();
	window.history.replaceState(null, "", "/");
});

describe("storeNavigation.goto guard enforcement", () => {
	it("refuses to navigate to lobby when the user is not in one", async () => {
		const { storeNavigation } = await import("$lib/stores/navigation.svelte");

		const applied = storeNavigation.goto("lobby");

		expect(applied).toBe(false);
		expect(storeNavigation.current).not.toBe("lobby");
	});

	it("allows navigation to lobby once the guard's condition is true", async () => {
		lobbyState.current = { invite_code: "ABCD" };
		const { storeNavigation } = await import("$lib/stores/navigation.svelte");

		const applied = storeNavigation.goto("lobby");

		expect(applied).toBe(true);
		expect(storeNavigation.current).toBe("lobby");
	});

	it("refuses lobbies when neither logged in nor guest", async () => {
		const { storeNavigation } = await import("$lib/stores/navigation.svelte");

		expect(storeNavigation.goto("lobbies")).toBe(false); // no auth, no guest
	});

	it("allows navigation to a screen with no guard regardless of other state", async () => {
		const { storeNavigation } = await import("$lib/stores/navigation.svelte");

		expect(storeNavigation.goto("decks")).toBe(true);
	});
});
