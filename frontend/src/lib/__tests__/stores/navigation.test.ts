import { describe, it, expect, vi, beforeEach } from "vitest";
import { StoreNavigation } from "$lib/stores/navigation.svelte";

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
vi.mock("$lib/stores/auth.svelte", () => ({
	storeAuth: { isLoggedIn: true, isGuest: false }
}));

describe("StoreNavigation magic link and deep links", () => {
	beforeEach(() => {
		window.history.replaceState(null, "", "/");
	});

	it("parses a /profile/verify/<code> URL into pendingVerifyCode", () => {
		const nav = new StoreNavigation("/profile/verify/123456");
		expect(nav.pendingVerifyCode).toBe("123456");
	});

	it("does not parse invalid verify codes", () => {
		const navShort = new StoreNavigation("/profile/verify/12345");
		expect(navShort.pendingVerifyCode).toBeNull();

		const navAlpha = new StoreNavigation("/profile/verify/abcdef");
		expect(navAlpha.pendingVerifyCode).toBeNull();

		const navLong = new StoreNavigation("/profile/verify/1234567");
		expect(navLong.pendingVerifyCode).toBeNull();

		const navProfile = new StoreNavigation("/profile");
		expect(navProfile.pendingVerifyCode).toBeNull();
	});

	it("parses invite code from initialPath", () => {
		const nav = new StoreNavigation("/invite/ABC123");
		expect(nav.pendingInviteCode).toBe("ABC123");
	});

	it("falls back to window.location.pathname when initialPath is omitted", () => {
		window.history.replaceState(null, "", "/profile/verify/654321");
		const nav = new StoreNavigation();
		expect(nav.pendingVerifyCode).toBe("654321");
	});
});
