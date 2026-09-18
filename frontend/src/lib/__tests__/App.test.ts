import { describe, it, expect, vi, beforeEach } from "vitest";
import { render } from "@testing-library/svelte";
import { storeAuth } from "$lib/stores/auth.svelte";
import { storeToast } from "$lib/stores/toast.svelte";
import { storeNavigation } from "$lib/stores/navigation.svelte";
import * as m from "$lib/paraglide/messages.js";
import App from "../../App.svelte";

// jsdom has no ResizeObserver; MainScreen's bind:clientHeight needs one to mount.
class StubResizeObserver {
	observe() {}
	unobserve() {}
	disconnect() {}
}
vi.stubGlobal("ResizeObserver", StubResizeObserver);

vi.mock("$lib/stores/analytics.svelte", () => ({ storeAnalytics: { track: vi.fn() } }));
vi.mock("$lib/stores/audio.svelte", () => ({ storeAudio: { init: vi.fn() } }));
vi.mock(import("$lib/stores/ws.svelte"), async (importOriginal) => {
	const actual = await importOriginal();
	return {
		...actual,
		ws: {
			connect: vi.fn().mockResolvedValue(undefined),
			on: vi.fn(() => () => {}),
			onOpen: vi.fn()
		}
	};
});
vi.mock("$lib/stores/game.svelte", () => ({ storeGame: { state: null } }));
vi.mock("$lib/stores/lobby.svelte", () => ({
	storeLobby: { isInLobby: false, join: vi.fn() }
}));
vi.mock("$stores/session", () => ({ installSessionResets: vi.fn() }));

vi.mock("$lib/stores/auth.svelte", () => ({
	storeAuth: {
		username: "eldyn",
		avatar: "",
		isLoggedIn: false,
		isGuest: false,
		emailVerified: false,
		checkSession: vi.fn().mockResolvedValue(false),
		onLoggedOut: vi.fn()
	}
}));

describe("App", () => {
	beforeEach(() => {
		vi.restoreAllMocks();
		vi.mocked(storeAuth).checkSession.mockResolvedValue(false);
		vi.mocked(storeAuth).isLoggedIn = false;
		vi.mocked(storeAuth).isGuest = false;
		vi.mocked(storeAuth).emailVerified = false;
		storeNavigation.pendingVerifyCode = null;
		storeNavigation.activeVerifyCode = null;
		storeNavigation.current = "main";
	});

	it("opens verification modal on session load for unverified users", async () => {
		vi.mocked(storeAuth).isLoggedIn = true;
		vi.mocked(storeAuth).emailVerified = false;
		const openModalSpy = vi.spyOn(storeNavigation, "openVerifyModal");

		render(App);

		await vi.waitFor(() => {
			expect(openModalSpy).toHaveBeenCalled();
		});
	});

	it("never opens verification modal for verified users", async () => {
		vi.mocked(storeAuth).isLoggedIn = true;
		vi.mocked(storeAuth).emailVerified = true;
		const openModalSpy = vi.spyOn(storeNavigation, "openVerifyModal");

		render(App);

		await vi.waitFor(() => {});
		expect(openModalSpy).not.toHaveBeenCalled();
	});

	it("never opens verification modal for logged out users", async () => {
		vi.mocked(storeAuth).isLoggedIn = false;
		vi.mocked(storeAuth).emailVerified = false;
		const openModalSpy = vi.spyOn(storeNavigation, "openVerifyModal");

		render(App);

		await vi.waitFor(() => {});
		expect(openModalSpy).not.toHaveBeenCalled();
	});

	it("waits for checkSession() before consuming pendingVerifyCode", async () => {
		let resolveSession!: (val: boolean) => void;
		const sessionPromise = new Promise<boolean>((res) => {
			resolveSession = res;
		});
		vi.mocked(storeAuth).checkSession.mockReturnValue(sessionPromise);
		vi.mocked(storeAuth).isLoggedIn = true;
		storeNavigation.pendingVerifyCode = "123456";
		const gotoSpy = vi.spyOn(storeNavigation, "goto");

		render(App);

		// Before checkSession completes, pendingVerifyCode is not consumed
		expect(storeNavigation.pendingVerifyCode).toBe("123456");
		expect(gotoSpy).not.toHaveBeenCalled();

		// Complete checkSession
		resolveSession(true);

		await vi.waitFor(() => {
			expect(gotoSpy).toHaveBeenCalledWith("profile");
			expect(storeNavigation.pendingVerifyCode).toBeNull();
			expect(storeNavigation.activeVerifyCode).toBe("123456");
		});
	});

	it("consumes pendingVerifyCode and sets activeVerifyCode when logged in", async () => {
		vi.mocked(storeAuth).isLoggedIn = true;
		storeNavigation.pendingVerifyCode = "123456";
		const gotoSpy = vi.spyOn(storeNavigation, "goto");

		render(App);

		await vi.waitFor(() => {
			expect(gotoSpy).toHaveBeenCalledWith("profile");
			expect(storeNavigation.pendingVerifyCode).toBeNull();
			expect(storeNavigation.activeVerifyCode).toBe("123456");
		});
	});

	it("retains pendingVerifyCode and opens login modal when not logged in", async () => {
		vi.mocked(storeAuth).isLoggedIn = false;
		storeNavigation.pendingVerifyCode = "123456";
		const gotoAuthSpy = vi.spyOn(storeNavigation, "gotoAuth");

		render(App);

		await vi.waitFor(() => {
			expect(gotoAuthSpy).toHaveBeenCalledWith("login");
			// Must NOT discard code so it can be consumed once user logs in
			expect(storeNavigation.pendingVerifyCode).toBe("123456");
			expect(storeNavigation.activeVerifyCode).toBeNull();
		});
	});
});
