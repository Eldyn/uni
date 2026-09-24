import { describe, it, expect, beforeEach, vi } from "vitest";

const { trackMock, paraglideState } = vi.hoisted(() => ({
	trackMock: vi.fn(),
	paraglideState: { locale: "en" }
}));

vi.mock("$lib/stores/analytics.svelte", () => ({ storeAnalytics: { track: trackMock } }));

vi.mock("$lib/paraglide/runtime.js", () => ({
	locales: ["en", "it"],
	getLocale: () => paraglideState.locale,
	setLocale: vi.fn((locale: string) => {
		paraglideState.locale = locale;
	})
}));

vi.mock("$lib/stores/auth.svelte", () => ({
	storeAuth: { username: "alice", isLoggedIn: true, isGuest: false }
}));
vi.mock("$lib/stores/game.svelte", () => ({ storeGame: { state: null } }));
vi.mock("$lib/stores/lobby.svelte", () => ({
	storeLobby: {
		get isInLobby() {
			return false;
		},
		leave: vi.fn()
	}
}));
vi.mock("$lib/stores/ws.svelte", () => ({
	ws: { onOpen: vi.fn(() => () => {}), on: vi.fn(() => () => {}) }
}));

import { storeI18n } from "$lib/stores/i18n.svelte";
import { StoreNavigation } from "$lib/stores/navigation.svelte";

function callsFor(event: string) {
	return trackMock.mock.calls.filter(([name]) => name === event).map(([, params]) => params);
}

describe("locale analytics", () => {
	beforeEach(() => {
		paraglideState.locale = "en";
		storeI18n.locale = "en";
		document.documentElement.lang = "";
		trackMock.mockClear();
	});

	it("emits locale_change on an explicit switch", () => {
		storeI18n.setLocale("it");

		expect(callsFor("locale_change")).toEqual([{ from: "en", to: "it" }]);
	});

	it("does not emit locale_change when the locale is unchanged", () => {
		storeI18n.setLocale("en");

		expect(callsFor("locale_change")).toHaveLength(0);
	});

	it("includes the active locale on screen_view", () => {
		const nav = new StoreNavigation("/");
		nav.goto("lobbies");

		const [params] = callsFor("screen_view");
		expect(params.screen).toBe("lobbies");
		expect(params.locale).toBe("en");
	});
});
