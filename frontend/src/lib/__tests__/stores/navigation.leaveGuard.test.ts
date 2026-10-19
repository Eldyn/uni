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

import { StoreNavigation } from "$lib/stores/navigation.svelte";

describe("leave guard", () => {
	beforeEach(() => {
		authState.isLoggedIn = true;
		window.localStorage.clear();
		window.history.replaceState(null, "", "/");
	});

	it("goto asks the guard and stays when it refuses", () => {
		const nav = new StoreNavigation();
		nav.goto("wardrobe");
		const guard = vi.fn(() => false);
		nav.setLeaveGuard(guard);

		expect(nav.goto("shop")).toBe(false);
		expect(guard).toHaveBeenCalledWith("shop");
		expect(nav.current).toBe("wardrobe");
	});

	it("goto moves on when the guard agrees or is removed", () => {
		const nav = new StoreNavigation();
		nav.goto("wardrobe");
		nav.setLeaveGuard(() => false);
		nav.setLeaveGuard(null);

		expect(nav.goto("shop")).toBe(true);
		expect(nav.current).toBe("shop");
	});

	it("a back gesture the guard refuses keeps the screen", () => {
		const nav = new StoreNavigation();
		nav.goto("wardrobe");
		nav.setLeaveGuard(() => false);
		window.dispatchEvent(
			new PopStateEvent("popstate", {
				state: { screen: "main", authModalOpen: false, authTab: "login", settingsOpen: false }
			})
		);

		expect(nav.current).toBe("wardrobe");
	});
});
