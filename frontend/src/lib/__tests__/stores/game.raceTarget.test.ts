import { describe, it, expect, beforeEach, vi } from "vitest";

// Mock ws.svelte with all ServerAction and ClientAction values
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
		MatchOver: "match_over",
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
		on(action: string, handler: any) {
			if (!(globalThis as any).__wsTestHandlers) {
				(globalThis as any).__wsTestHandlers = {};
			}
			(globalThis as any).__wsTestHandlers[action] = handler;
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
import { storeAuth } from "$lib/stores/auth.svelte";

function handler(action: string): (payload: any) => void {
	const fn = (globalThis as any).__wsTestHandlers?.[action];
	if (!fn) throw new Error(`${action} handler was not registered`);
	return fn;
}

function feedRaceSnapshot() {
	handler("match_state_updated")({
		match_state: {
			status: "playing",
			current_player: "carol",
			mode: "race",
			race_target: 2,
			placements: ["alice", "bob"],
			players: [{ username: "carol", seat: 2, is_current: true, card_count: 3, is_bot: false }]
		},
		action_required: null
	});
}

describe("storeGame carries race_target through the snapshot parser", () => {
	beforeEach(() => {
		storeGame.reset();
		storeAuth.username = "alice";
	});

	it("keeps race_target from a raw snapshot", () => {
		feedRaceSnapshot();
		expect(storeGame.state?.mode).toBe("race");
		expect(storeGame.state?.race_target).toBe(2);
		expect(storeGame.state?.placements).toEqual(["alice", "bob"]);
	});

	it("scores a finisher as a win and a left-behind player as a loss at match end", () => {
		feedRaceSnapshot();
		const finalPlacements = ["alice", "bob", "carol"];
		handler("match_over")({ winner: "alice", placements: finalPlacements });

		storeAuth.username = "alice";
		expect(storeGame.raceOutcome).toBe("win");
		storeAuth.username = "carol";
		expect(storeGame.raceOutcome).toBe("lose");
	});
});
