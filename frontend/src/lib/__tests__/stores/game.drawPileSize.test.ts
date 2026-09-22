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
			// Store handler in global for access in tests
			if (!(globalThis as any).__wsTestHandlers) {
				(globalThis as any).__wsTestHandlers = {};
			}
			(globalThis as any).__wsTestHandlers[action] = handler;
			console.log(`Mock ws.on called with action: "${action}"`);
			return () => {}; // unsubscribe
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

describe("storeGame draw_pile_size", () => {
	beforeEach(() => {
		storeGame.state = null;
	});

	it("parses draw_pile_size off the match_state payload", () => {
		const handlers = (globalThis as any).__wsTestHandlers || {};
		const handler = handlers["match_state_updated"];
		console.log("Available handlers:", Object.keys(handlers));
		if (!handler) {
			throw new Error(
				`MatchStateUpdated handler was not registered. Registered: ${Object.keys(handlers).join(
					", "
				)}`
			);
		}

		// Call the handler with a minimal valid payload including draw_pile.count
		handler({
			match_state: {
				current_player: "test_player",
				players: [],
				draw_pile: { count: 42 }
			},
			action_required: null
		});

		expect(storeGame.state?.draw_pile_size).toBe(42);
	});

	it("parses discard_pile_size off the match_state payload", () => {
		const handler = (globalThis as any).__wsTestHandlers?.["match_state_updated"];
		if (!handler) throw new Error("MatchStateUpdated handler was not registered.");

		handler({
			match_state: {
				current_player: "test_player",
				players: [],
				draw_pile: { count: 0 },
				discard_pile: { count: 13 }
			},
			action_required: null
		});

		expect(storeGame.state?.discard_pile_size).toBe(13);
	});
});
