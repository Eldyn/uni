import { describe, it, expect, beforeEach, vi } from "vitest";

vi.mock("$lib/stores/ws.svelte", () => ({
	ServerAction: {
		Success: "success",
		Error: "error",
		LobbyList: "lobby_list",
		LobbyJoined: "lobby_joined",
		LobbyUpdated: "lobby_updated",
		LoadoutChanged: "loadout_changed",
		LobbyLeft: "lobby_left",
		LobbyEvicted: "lobby_evicted",
		MatchStateUpdated: "match_state_updated",
		MatchOver: "match_over",
		MatchEvent: "match_event",
		ChatMessage: "chat_message",
		ChatHistory: "chat_history",
		FriendList: "friend_list",
		Metadata: "metadata"
	},
	ClientAction: {
		MatchPlayCard: "match_play_card",
		MatchDrawCard: "match_draw_card",
		MatchSubmitInput: "match_submit_input",
		MatchExit: "match_exit",
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
		LobbyStartMatch: "lobby_start_match",
		ChatSend: "chat_send",
		ChatHistoryRequest: "chat_history_request",
		FriendRequest: "friend_request",
		FriendResponse: "friend_response",
		FriendListRequest: "friend_list_request",
		MetadataRequest: "metadata_request"
	},
	ws: {
		on(action: string, handler: (data: Record<string, unknown>) => void) {
			const registry = ((globalThis as Record<string, unknown>).__wsTestHandlers ??= {}) as Record<
				string,
				(data: Record<string, unknown>) => void
			>;
			registry[action] = handler;
			return () => {};
		},
		onOpen() {
			return () => {};
		},
		onClose() {
			return () => {};
		},
		emit() {},
		emitAndWait: async () => ({ ok: false })
	}
}));

import { storeGame } from "$lib/stores/game.svelte";
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

function snapshot(loadout: unknown) {
	return {
		match_state: {
			current_player: "alice",
			players: [
				{ username: "alice", card_count: 1, is_bot: false, loadout },
				{ username: "bob", card_count: 2, is_bot: true }
			]
		}
	};
}

describe("storeGame loadouts", () => {
	beforeEach(() => {
		storeGame.state = null;
	});

	it("game player parses a loadout", () => {
		handler("match_state_updated")(snapshot(sample));

		expect(storeGame.state?.players[0].loadout).toEqual(sample);
	});

	it("game player with a malformed loadout parses to null", () => {
		handler("match_state_updated")(snapshot({ shape: 42, skin: 7, slots: [] }));

		expect(storeGame.state?.players[0].loadout).toBeNull();
	});

	it("loadout_changed updates the matching game player", () => {
		handler("match_state_updated")(snapshot(sample));

		handler("loadout_changed")({ username: "alice", loadout: other });

		expect(storeGame.state?.players[0].loadout).toEqual(other);
	});

	it("loadout_changed for an unknown player is ignored", () => {
		handler("match_state_updated")(snapshot(sample));

		handler("loadout_changed")({ username: "carol", loadout: other });

		expect(storeGame.state?.players[0].loadout).toEqual(sample);
	});
});
