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
	storeLobby: {
		get isInLobby() {
			return false;
		},
		leave: vi.fn()
	}
}));
vi.mock("$lib/stores/auth.svelte", () => ({ storeAuth: { isLoggedIn: true, isGuest: false } }));

beforeEach(() => {
	openHandlers.length = 0;
	vi.resetModules();
	window.localStorage.clear();
	window.history.replaceState(null, "", "/");
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
			storeLobby: {
				get isInLobby() {
					return true;
				},
				leave: vi.fn()
			}
		}));
		const { storeNavigation } = await import("$lib/stores/navigation.svelte");

		storeNavigation.goto("lobby");

		expect(window.localStorage.getItem("currentScreen")).not.toBe("lobby");
	});

	it("never writes lobby or game to localStorage via a popstate gesture", async () => {
		vi.doMock("$lib/stores/lobby.svelte", () => ({
			storeLobby: {
				get isInLobby() {
					return true;
				},
				leave: vi.fn()
			}
		}));
		await import("$lib/stores/navigation.svelte");

		window.dispatchEvent(
			new PopStateEvent("popstate", {
				state: {
					screen: "lobby",
					authModalOpen: false,
					authTab: "login",
					settingsOpen: false
				}
			})
		);

		expect(window.localStorage.getItem("currentScreen")).not.toBe("lobby");
	});

	it("seeds the current screen from the URL on a hard refresh/deep link", async () => {
		window.history.replaceState(null, "", "/browse");
		const { storeNavigation } = await import("$lib/stores/navigation.svelte");

		expect(storeNavigation.current).toBe("lobbies");
		expect(window.location.pathname).toBe("/browse");
	});

	it("falls back to main when a deep link's guard fails", async () => {
		vi.doMock("$lib/stores/auth.svelte", () => ({
			storeAuth: { isLoggedIn: false, isGuest: false }
		}));
		window.history.replaceState(null, "", "/browse");
		const { storeNavigation } = await import("$lib/stores/navigation.svelte");

		expect(storeNavigation.current).toBe("main");
		expect(window.location.pathname).toBe("/");
	});

	it("clears a stale 'settings' value from localStorage instead of navigating to it", async () => {
		window.localStorage.setItem("currentScreen", "settings");
		const { storeNavigation } = await import("$lib/stores/navigation.svelte");

		for (const handler of openHandlers) handler();

		expect(window.localStorage.getItem("currentScreen")).toBeNull();
		expect(storeNavigation.current).toBe("main");
	});
});
