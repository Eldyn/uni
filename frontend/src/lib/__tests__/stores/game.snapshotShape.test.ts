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

function snapshotHandler(): (payload: any) => void {
	const handler = (globalThis as any).__wsTestHandlers?.["match_state_updated"];
	if (!handler) throw new Error("MatchStateUpdated handler was not registered");
	return handler;
}

describe("storeGame maps the ECS snapshot shape", () => {
	beforeEach(() => {
		storeGame.state = null;
	});

	it("maps a full new-shape match_state into GameState", () => {
		snapshotHandler()({
			match_state: {
				status: "playing",
				round: 3,
				direction: -1,
				active_type: "blue",
				current_player: "alice",
				winner: null,
				placements: ["carol"],
				players: [
					{
						username: "alice",
						seat: 0,
						is_current: true,
						card_count: 2,
						is_bot: false,
						hand: [
							{
								slot: 0,
								card: 12345,
								kind: "red_5",
								color: "red",
								value: "5",
								can_play: true
							}
						],
						statuses: []
					},
					{
						username: "bob",
						seat: 1,
						is_current: false,
						card_count: 4,
						is_bot: true,
						statuses: []
					}
				],
				draw_pile: { count: 42 },
				discard_pile: {
					count: 7,
					top: { card: 999, kind: "blue_skip", color: "blue", value: "skip" }
				},
				window: null,
				prompts: [],
				seq_watermark: 17
			},
			action_required: null
		});

		const state = storeGame.state!;
		expect(state.current_turn).toBe("alice");
		expect(state.active_type).toBe("blue");
		expect(state.play_direction).toBe(-1);
		expect(state.top_card?.id).toBe(999);
		expect(state.top_card?.type).toBe("blue");
		expect(state.top_card?.value).toBe("skip");
		expect(state.draw_pile_size).toBe(42);
		expect(state.pending_draws).toBe(0);
		expect(state.placements).toEqual(["carol"]);
		expect(state.seq_watermark).toBe(17);

		const alice = state.players.find((p) => p.username === "alice")!;
		expect(alice.hand?.map((c) => [c.id, c.type, c.value, c.can_play])).toEqual([
			[12345, "red", "5", true]
		]);
		const bob = state.players.find((p) => p.username === "bob")!;
		expect(bob.hand).toBeUndefined();
	});

	it("defaults absent optional fields (null active_type, no piles)", () => {
		snapshotHandler()({
			match_state: {
				current_player: "alice",
				active_type: null,
				players: []
			},
			action_required: null
		});

		const state = storeGame.state!;
		expect(state.active_type).toBe("white");
		expect(state.play_direction).toBe(1);
		expect(state.top_card).toBeUndefined();
		expect(state.draw_pile_size).toBe(0);
		expect(state.seq_watermark).toBeUndefined();
	});
});
