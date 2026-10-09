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

function emptyStreaks(): Record<string, unknown> {
	return {};
}

function matchState(overrides: Record<string, unknown> = {}) {
	return {
		current_player: "alice",
		active_type: "red",
		direction: 1,
		pending_draws: 0,
		pending_play_drawn: null,
		turn_deadline_ms: 0,
		players: [
			{ username: "alice", card_count: 3, is_bot: false },
			{ username: "bob", card_count: 3, is_bot: false }
		],
		seq_watermark: 9,
		context: {
			active_color: "green",
			direction: -1,
			draw_debt: 6,
			hold: { player: "bob", held: true },
			timers: {
				turn_deadline_ms: 0,
				turn_remaining_ms: 12000,
				window_deadline_ms: 0,
				window_remaining_ms: 0
			},
			last_event_sequence: 8,
			streaks: emptyStreaks()
		},
		...overrides
	};
}

describe("storeGame reads the snapshot context", () => {
	beforeEach(() => {
		storeGame.state = null;
		storeGame.lastSeq = null;
	});

	it("takes color, direction, debt and hold from the context", () => {
		snapshotHandler()({ match_state: matchState(), action_required: null });

		const state = storeGame.state!;
		expect(state.active_type).toBe("green");
		expect(state.play_direction).toBe(-1);
		expect(state.pending_draws).toBe(6);
		expect(state.pendingPlayDrawn).toEqual({ player: "bob" });
		expect(state.context?.streaks).toEqual({});
	});

	it("shows the card of the hold to its owner only", () => {
		snapshotHandler()({
			match_state: matchState({
				context: {
					...matchState().context,
					hold: { player: "alice", held: true, card: 321 }
				}
			}),
			action_required: null
		});

		expect(storeGame.state!.pendingPlayDrawn).toEqual({ player: "alice", card: 321 });
	});

	it("continues the sequence from the last client event", () => {
		snapshotHandler()({
			match_state: matchState({
				context: { ...matchState().context, last_event_sequence: 11 }
			}),
			action_required: null
		});

		expect(storeGame.lastSeq).toBe(11);
	});

	it("falls back to the top-level fields when the context is absent", () => {
		const { context: _context, ...withoutContext } = matchState();
		snapshotHandler()({ match_state: withoutContext, action_required: null });

		const state = storeGame.state!;
		expect(state.active_type).toBe("red");
		expect(state.play_direction).toBe(1);
		expect(storeGame.lastSeq).toBe(8);
	});

	it("clears the hold when a later snapshot has a context and no hold", () => {
		snapshotHandler()({ match_state: matchState(), action_required: null });
		expect(storeGame.state!.pendingPlayDrawn).toEqual({ player: "bob" });

		snapshotHandler()({
			match_state: matchState({ context: { ...matchState().context, hold: null } }),
			action_required: null
		});

		expect(storeGame.state!.pendingPlayDrawn).toBeNull();
	});
});
