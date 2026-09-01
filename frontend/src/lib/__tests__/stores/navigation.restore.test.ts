import { describe, it, expect, vi, beforeEach } from "vitest";

const openHandlers: Array<() => void> = [];

vi.mock("$lib/stores/analytics.svelte", () => ({ storeAnalytics: { track: vi.fn() } }));
vi.mock("$lib/stores/ws.svelte", () => ({
	ws: {
		onOpen: vi.fn((handler: () => void) => {
			openHandlers.push(handler);
			return () => {};
		})
	}
}));
vi.mock("$lib/stores/game.svelte", () => ({ storeGame: { state: null, returnToLobby: vi.fn() } }));
vi.mock("$lib/stores/lobby.svelte", () => ({
	storeLobby: { get isInLobby() { return false; }, leave: vi.fn() }
}));
vi.mock("$lib/stores/auth.svelte", () => ({ storeAuth: { isLoggedIn: true, isGuest: false } }));

beforeEach(() => {
	openHandlers.length = 0;
	vi.resetModules();
	window.localStorage.clear();
	window.history.replaceState(null, "");
});

describe("navigation restore on connect", () => {
	it("does not restore to lobby from a stale localStorage entry when not in one", async () => {
		window.localStorage.setItem("currentScreen", "lobby");
		const { storeNavigation } = await import("$lib/stores/navigation.svelte");

		for (const handler of openHandlers) handler();

		expect(storeNavigation.current).not.toBe("lobby");
	});

	it("restores a stateless screen normally", async () => {
		window.localStorage.setItem("currentScreen", "lobbies");
		const { storeNavigation } = await import("$lib/stores/navigation.svelte");

		for (const handler of openHandlers) handler();

		expect(storeNavigation.current).toBe("lobbies");
	});

	it("never writes lobby or game to localStorage", async () => {
		vi.doMock("$lib/stores/lobby.svelte", () => ({
			storeLobby: { get isInLobby() { return true; }, leave: vi.fn() }
		}));
		const { storeNavigation } = await import("$lib/stores/navigation.svelte");

		storeNavigation.goto("lobby");

		expect(window.localStorage.getItem("currentScreen")).not.toBe("lobby");
	});
});
