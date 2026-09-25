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

	it("opens and closes verify modal", () => {
		const nav = new StoreNavigation();
		expect(nav.isVerifyModalOpen).toBe(false);

		nav.openVerifyModal();
		expect(nav.isVerifyModalOpen).toBe(true);

		nav.closeVerifyModal();
		expect(nav.isVerifyModalOpen).toBe(false);
	});

	it("parses a /reset-password/<43-char token> URL into pendingResetToken", () => {
		const token = "aB_-1234567890abcdefghijklmnopqrstuvwxyzABC"; // 43 chars
		const nav = new StoreNavigation(`/reset-password/${token}`);
		expect(nav.pendingResetToken).toBe(token);
	});

	it("does not parse malformed or short reset tokens", () => {
		const navShort = new StoreNavigation("/reset-password/abc");
		expect(navShort.pendingResetToken).toBeNull();

		// 44 chars is too long.
		const navLong = new StoreNavigation(`/reset-password/${"a".repeat(44)}`);
		expect(navLong.pendingResetToken).toBeNull();

		// Invalid character (slash would break the segment anyway; use '.').
		const navInvalid = new StoreNavigation(`/reset-password/${"a".repeat(42)}.`);
		expect(navInvalid.pendingResetToken).toBeNull();

		const navPlain = new StoreNavigation("/reset-password");
		expect(navPlain.pendingResetToken).toBeNull();
	});

	it("opens and closes reset modal", () => {
		const nav = new StoreNavigation();
		expect(nav.isResetModalOpen).toBe(false);

		nav.openResetModal();
		expect(nav.isResetModalOpen).toBe(true);

		nav.closeResetModal();
		expect(nav.isResetModalOpen).toBe(false);
	});
});
