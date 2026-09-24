import { describe, it, expect, beforeEach, vi } from "vitest";

const { trackMock } = vi.hoisted(() => ({ trackMock: vi.fn() }));

vi.mock("$lib/stores/analytics.svelte", () => ({ storeAnalytics: { track: trackMock } }));
vi.mock("$lib/stores/auth.svelte", () => ({ storeAuth: { username: "alice", isLoggedIn: true } }));
vi.mock("$lib/stores/audio.svelte", () => ({ storeAudio: { playSfx: vi.fn() } }));
vi.mock("$lib/stores/game.svelte", () => ({ storeGame: { state: null } }));
vi.mock("$lib/stores/navigation.svelte", () => ({ storeNavigation: { goto: vi.fn() } }));
vi.mock("$lib/stores/toast.svelte", () => ({
	storeToast: { error: vi.fn(), success: vi.fn() }
}));
vi.mock("$lib/stores/i18n.svelte", () => ({
	storeI18n: { locale: "en", locales: ["en", "it"], setLocale: vi.fn() }
}));

vi.mock("$lib/stores/ws.svelte", () => ({
	ServerAction: {
		Success: "success",
		Error: "error",
		LobbyList: "lobby_list",
		LobbyJoined: "lobby_joined",
		LobbyUpdated: "lobby_updated",
		LobbyLeft: "lobby_left",
		LobbyEvicted: "lobby_evicted",
		MatchStateUpdated: "match_state_updated",
		MatchEvent: "match_event",
		MatchOver: "match_over"
	},
	ClientAction: {
		LobbyList: "lobby_list",
		LobbyCreate: "lobby_create",
		LobbyJoin: "lobby_join",
		LobbyRejoin: "lobby_rejoin",
		LobbyQuickJoin: "lobby_quick_join",
		LobbyLeave: "lobby_leave",
		LobbyToggleReady: "lobby_toggle_ready",
		LobbyPromote: "lobby_promote",
		LobbyKick: "lobby_kick",
		LobbyUpdateSettings: "lobby_update_settings",
		LobbyStartMatch: "lobby_start_match"
	},
	ws: {
		on() {
			return () => {};
		},
		onOpen() {
			return () => {};
		},
		onClose() {
			return () => {};
		},
		connect: vi.fn(async () => {}),
		emit: vi.fn(),
		emitAndWait: vi.fn(async () => ({ ok: true, get: () => undefined }))
	}
}));

import { storeLobby } from "$lib/stores/lobby.svelte";

function callsFor(event: string) {
	return trackMock.mock.calls.filter(([name]) => name === event).map(([, params]) => params);
}

function lobby() {
	return {
		host: "alice",
		invite_code: "ABC123",
		members: [
			{ username: "alice", is_bot: false, is_ready: true },
			{ username: "bob", is_bot: false, is_ready: true }
		],
		settings: { is_public: true, active_mods: [] }
	};
}

describe("storeLobby match analytics", () => {
	beforeEach(() => {
		trackMock.mockClear();
		storeLobby.reset();
		(storeLobby as any).current = lobby();
	});

	it("stamps match_start with a match_key", async () => {
		await storeLobby.startMatch();

		const [params] = callsFor("match_start");
		expect(typeof params.match_key).toBe("string");
		expect(params.match_key.length).toBeGreaterThan(0);
		expect(callsFor("rematch")).toHaveLength(0);
	});

	it("emits rematch on a second match in the same lobby", async () => {
		await storeLobby.startMatch();
		const firstKey = callsFor("match_start")[0].match_key;

		await storeLobby.startMatch();

		const [params] = callsFor("rematch");
		expect(params.match_index).toBe(2);
		expect(params.player_count).toBe(2);
		expect(params.match_key).not.toBe(firstKey);
	});

	it("emits lobby_abandoned when leaving before any match", async () => {
		await storeLobby.leave();

		const [params] = callsFor("lobby_abandoned");
		expect(params.is_host).toBe(true);
		expect(params.member_count).toBe(2);
		expect(callsFor("lobby_leave")).toHaveLength(1);
	});

	it("does not emit lobby_abandoned when leaving after a match", async () => {
		await storeLobby.startMatch();
		trackMock.mockClear();

		await storeLobby.leave();

		expect(callsFor("lobby_abandoned")).toHaveLength(0);
		expect(callsFor("lobby_leave")).toHaveLength(1);
	});
});
