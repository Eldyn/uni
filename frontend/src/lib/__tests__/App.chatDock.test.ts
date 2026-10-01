import { describe, it, expect, vi, beforeEach } from "vitest";
import { render } from "@testing-library/svelte";
import { storeAuth } from "$lib/stores/auth.svelte";
import { storeNavigation } from "$lib/stores/navigation.svelte";
import * as m from "$lib/paraglide/messages.js";
import App from "../../App.svelte";

class StubResizeObserver {
	observe() {}
	unobserve() {}
	disconnect() {}
}
vi.stubGlobal("ResizeObserver", StubResizeObserver);

vi.mock("$lib/stores/analytics.svelte", () => ({ storeAnalytics: { track: vi.fn() } }));
vi.mock("$lib/stores/audio.svelte", () => ({ storeAudio: { init: vi.fn() } }));
vi.mock("$components/game/GameScreen.svelte", () => ({ default: () => null }));
vi.mock("$components/game/GameLoader.svelte", () => ({ default: () => null }));
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
		isLoggedIn: true,
		isGuest: false,
		emailVerified: true,
		checkSession: vi.fn().mockResolvedValue(true),
		onLoggedOut: vi.fn()
	}
}));

describe("App chat dock", () => {
	beforeEach(() => {
		vi.mocked(storeAuth).isLoggedIn = true;
		vi.mocked(storeAuth).isGuest = false;
	});

	it("mounts the chat launcher during a match", async () => {
		storeNavigation.current = "game";
		const { findByLabelText } = render(App);
		const launcher = await findByLabelText(m.chat_dock_open({}));
		expect(launcher.classList.contains("chat-launcher-in-game")).toBe(true);
	});

	it("advertises no keyboard shortcut during a match, where it does nothing", async () => {
		storeNavigation.current = "game";
		const { findByLabelText } = render(App);
		const launcher = await findByLabelText(m.chat_dock_open({}));
		expect(launcher.hasAttribute("aria-keyshortcuts")).toBe(false);
	});

	it("keeps the launcher on the bottom edge outside a match", async () => {
		storeNavigation.current = "main";
		const { findByLabelText } = render(App);
		const launcher = await findByLabelText(m.chat_dock_open({}));
		expect(launcher.classList.contains("chat-launcher-in-game")).toBe(false);
		expect(launcher.hasAttribute("aria-keyshortcuts")).toBe(true);
	});

	it("does not mount the dock for a logged-out visitor", async () => {
		vi.mocked(storeAuth).isLoggedIn = false;
		storeNavigation.current = "main";
		const { queryByLabelText } = render(App);
		await vi.waitFor(() => {});
		expect(queryByLabelText(m.chat_dock_open({}))).toBeNull();
	});
});
