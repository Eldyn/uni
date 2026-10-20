import { describe, it, expect, vi, beforeEach } from "vitest";
import { render } from "@testing-library/svelte";
import { storeAuth } from "$lib/stores/auth.svelte";
import { storeAvatar } from "$stores/avatar.svelte";
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
vi.mock("$lib/stores/audio.svelte", () => ({ storeAudio: { init: vi.fn(), playSfx: vi.fn() } }));
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

const { fetchMine } = vi.hoisted(() => ({ fetchMine: vi.fn() }));
vi.mock("$stores/avatar.svelte", () => ({
	storeAvatar: { loadout: null, status: "idle", fetchMine }
}));

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
		(storeAvatar as { loadout: unknown }).loadout = null;
		(storeAvatar as { status: string }).status = "idle";
		fetchMine.mockReset();
		fetchMine.mockResolvedValue(undefined);
		storeNavigation.pendingVerifyCode = null;
		storeNavigation.activeVerifyCode = null;
		storeNavigation.current = "main";
		storeNavigation.initialScreen = null;
		storeNavigation.deferredScreen = null;
	});

	it("hydrates the avatar loadout after a logged-in session check", async () => {
		vi.mocked(storeAuth).isLoggedIn = true;

		render(App);

		await vi.waitFor(() => {
			expect(fetchMine).toHaveBeenCalledTimes(1);
		});
	});

	it("hydrates the loadout before applying a deferred deep link", async () => {
		vi.mocked(storeAuth).isLoggedIn = true;
		storeNavigation.deferredScreen = "wardrobe";
		const calls: string[] = [];
		fetchMine.mockImplementation(async () => {
			calls.push("fetch");
		});
		const applySpy = vi.spyOn(storeNavigation, "applyInitialScreen").mockImplementation(() => {
			calls.push("apply");
		});

		render(App);

		await vi.waitFor(() => {
			expect(applySpy).toHaveBeenCalled();
		});
		expect(calls).toEqual(["fetch", "apply"]);
	});

	it("does not hydrate the loadout for a guest", async () => {
		vi.mocked(storeAuth).isGuest = true;

		render(App);

		await vi.waitFor(() => {
			expect(vi.mocked(storeAuth).checkSession).toHaveBeenCalled();
		});
		expect(fetchMine).not.toHaveBeenCalled();
	});

	it("does not refetch a loadout that is already hydrated", async () => {
		vi.mocked(storeAuth).isLoggedIn = true;
		(storeAvatar as { loadout: unknown }).loadout = { shape: "uni:shape_round" };

		render(App);

		await vi.waitFor(() => {
			expect(vi.mocked(storeAuth).checkSession).toHaveBeenCalled();
		});
		expect(fetchMine).not.toHaveBeenCalled();
	});

	it("does not start a second fetch while one is already in flight", async () => {
		vi.mocked(storeAuth).isLoggedIn = true;
		(storeAvatar as { status: string }).status = "loading";

		render(App);

		await vi.waitFor(() => {
			expect(vi.mocked(storeAuth).checkSession).toHaveBeenCalled();
		});
		expect(fetchMine).not.toHaveBeenCalled();
	});

	it("applies a deferred /wardrobe deep link once the session resolves", async () => {
		vi.mocked(storeAuth).isLoggedIn = true;
		storeNavigation.deferredScreen = "wardrobe";

		render(App);

		await vi.waitFor(() => {
			expect(storeNavigation.current).toBe("wardrobe");
		});
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
