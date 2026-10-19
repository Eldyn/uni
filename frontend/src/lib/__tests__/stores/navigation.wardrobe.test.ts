import { describe, it, expect, vi, beforeEach } from "vitest";

vi.mock("$lib/stores/analytics.svelte", () => ({ storeAnalytics: { track: vi.fn() } }));
vi.mock("$lib/stores/ws.svelte", () => ({ ws: { onOpen: vi.fn() } }));
vi.mock("$lib/stores/game.svelte", () => ({ storeGame: { state: null, returnToLobby: vi.fn() } }));
vi.mock("$lib/stores/lobby.svelte", () => ({
	storeLobby: {
		get isInLobby() {
			return false;
		},
		leave: vi.fn()
	}
}));

const authState = vi.hoisted(() => ({ isLoggedIn: false, isGuest: false }));
vi.mock("$lib/stores/auth.svelte", () => ({
	storeAuth: {
		get isLoggedIn() {
			return authState.isLoggedIn;
		},
		get isGuest() {
			return authState.isGuest;
		}
	}
}));

import { StoreNavigation, pathForScreen, screenForPath } from "$lib/stores/navigation.svelte";

describe("wardrobe navigation", () => {
	beforeEach(() => {
		authState.isLoggedIn = false;
		authState.isGuest = false;
		window.localStorage.clear();
		window.history.replaceState(null, "", "/");
	});

	it("maps the wardrobe screen to /wardrobe and back", () => {
		expect(pathForScreen("wardrobe")).toBe("/wardrobe");
		expect(screenForPath("/wardrobe")).toBe("wardrobe");
	});

	it("reaches the wardrobe for a logged-in account", () => {
		authState.isLoggedIn = true;
		const nav = new StoreNavigation();

		expect(nav.goto("wardrobe")).toBe(true);
		expect(nav.current).toBe("wardrobe");
	});

	it("reaches the wardrobe for a guest, whose screen shows the register prompt", () => {
		authState.isGuest = true;
		const nav = new StoreNavigation();

		expect(nav.goto("wardrobe")).toBe(true);
		expect(nav.current).toBe("wardrobe");
	});

	it("refuses the wardrobe when neither logged in nor guest", () => {
		const nav = new StoreNavigation();

		expect(nav.goto("wardrobe")).toBe(false);
		expect(nav.current).not.toBe("wardrobe");
	});

	it("defers a cold load of /wardrobe until the account session resolves", () => {
		const nav = new StoreNavigation("/wardrobe");
		// The guard fails at construction: auth has not been restored yet.
		expect(nav.current).toBe("main");
		expect(nav.initialScreen).toBe("wardrobe");

		authState.isLoggedIn = true;
		nav.applyInitialScreen();

		expect(nav.current).toBe("wardrobe");
	});

	it("lands a guest who cold loads /wardrobe on the wardrobe", () => {
		const nav = new StoreNavigation("/wardrobe");
		authState.isGuest = true;
		nav.applyInitialScreen();

		expect(nav.current).toBe("wardrobe");
	});

	it("keeps an anonymous cold load of /wardrobe on home", () => {
		const nav = new StoreNavigation("/wardrobe");
		nav.applyInitialScreen();

		expect(nav.current).toBe("main");
	});
});
