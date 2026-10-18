import { describe, it, expect, vi, beforeEach } from "vitest";

vi.mock("$lib/stores/analytics.svelte", () => ({ storeAnalytics: { track: vi.fn() } }));
vi.mock("$lib/stores/auth.svelte", () => ({ storeAuth: { username: "alice", isLoggedIn: true } }));
vi.mock("$lib/stores/audio.svelte", () => ({ storeAudio: { playSfx: vi.fn() } }));
vi.mock("$lib/stores/game.svelte", () => ({ storeGame: { state: null } }));
vi.mock("$lib/stores/navigation.svelte", () => ({ storeNavigation: { goto: vi.fn() } }));
vi.mock("$lib/stores/toast.svelte", () => ({
	storeToast: { error: vi.fn(), success: vi.fn() }
}));
vi.mock("$lib/stores/i18n.svelte", () => ({
	storeI18n: { locale: "en", locales: ["en"], setLocale: vi.fn() }
}));

vi.mock("$lib/stores/ws.svelte", () => ({
	ws: {
		on(action: string, handler: (data: Record<string, unknown>) => void) {
			const registry = ((globalThis as Record<string, unknown>).__wsTestHandlers ??= {}) as Record<
				string,
				(data: Record<string, unknown>) => void
			>;
			registry[action] = handler;
			return () => {};
		},
		onOpen: vi.fn(() => () => {}),
		onClose: vi.fn(() => () => {}),
		connect: vi.fn(async () => {}),
		emit: vi.fn(),
		emitAndWait: vi.fn(async () => ({ ok: true, get: () => undefined }))
	},
	ServerAction: {
		LobbyJoined: "lobby_joined",
		LobbyUpdated: "lobby_updated",
		LobbyLeft: "lobby_left",
		LobbyEvicted: "lobby_evicted",
		LoadoutChanged: "loadout_changed",
		MatchStateUpdated: "match_state_updated"
	},
	ClientAction: {
		LobbyList: "lobby_list",
		LobbyLeave: "lobby_leave"
	}
}));

import { storeLobby } from "$lib/stores/lobby.svelte";
import type { Loadout } from "$lib/avatar/types";

const sample: Loadout = {
	shape: "uni:shape_round",
	skin: "uni:skin_basic_1",
	slots: { eyes: { item: "uni:eyes_dot", colours: [] } }
};

const other: Loadout = {
	shape: "uni:shape_square",
	skin: "uni:skin_basic_2",
	slots: { mouth: { item: "uni:mouth_line", colours: [] } }
};

function handler(action: string): (data: Record<string, unknown>) => void {
	const registry = (globalThis as Record<string, unknown>).__wsTestHandlers as
		Record<string, (data: Record<string, unknown>) => void> | undefined;
	const found = registry?.[action];
	if (!found) throw new Error(`no ws handler registered for ${action}`);
	return found;
}

function member(overrides: Record<string, unknown> = {}) {
	return {
		username: "alice",
		is_connected: true,
		is_host: true,
		is_bot: false,
		is_ready: true,
		...overrides
	};
}

function lobby(members: unknown[]) {
	return {
		invite_code: "ABC123",
		host: "alice",
		name: "Test",
		members,
		settings: { active_mods: [], allow_bot_takeover: false, max_players: 4 }
	};
}

describe("storeLobby loadouts", () => {
	beforeEach(() => {
		storeLobby.reset();
	});

	it("lobby member parses a loadout", () => {
		handler("lobby_joined")({ lobby: lobby([member({ loadout: sample })]) });

		expect(storeLobby.current?.members[0].loadout).toEqual(sample);
	});

	it("lobby member with a malformed loadout parses to null", () => {
		handler("lobby_joined")({
			lobby: lobby([member({ loadout: { shape: 42, skin: 7, slots: [] } })])
		});

		expect(storeLobby.current?.members[0].loadout).toBeNull();
	});

	it("loadout_changed updates the matching lobby member", () => {
		handler("lobby_joined")({ lobby: lobby([member({ loadout: sample })]) });

		handler("loadout_changed")({ username: "alice", loadout: other });

		expect(storeLobby.current?.members[0].loadout).toEqual(other);
	});

	it("loadout_changed for an unknown member is ignored", () => {
		handler("lobby_joined")({ lobby: lobby([member({ loadout: sample })]) });

		handler("loadout_changed")({ username: "bob", loadout: other });

		expect(storeLobby.current?.members[0].loadout).toEqual(sample);
	});
});
