import { describe, it, expect, beforeEach, vi } from "vitest";

// Mock ws.svelte with all ServerAction and ClientAction values.
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

function handler(action: string): (payload: any) => void {
	const h = (globalThis as any).__wsTestHandlers?.[action];
	if (!h) throw new Error(`${action} handler was not registered`);
	return h;
}

function frame(seq: number, type: string, payload: unknown = {}) {
	return { action: "match_event", seq, type, payload };
}

function snapshot(extra: Record<string, unknown> = {}) {
	return {
		match_state: {
			current_player: "alice",
			active_type: "red",
			players: [{ username: "alice", card_count: 5, is_bot: false }],
			draw_pile: { count: 40 },
			...extra
		},
		action_required: null
	};
}

describe("storeGame turn deadline", () => {
	beforeEach(() => {
		storeGame.reset();
	});

	it("arms the countdown from a snapshot turn_deadline_ms", () => {
		const deadline = Date.now() + 10_000;
		handler("match_state_updated")(snapshot({ turn_deadline_ms: deadline }));

		expect(storeGame.state?.turn_deadline_ms).toBe(deadline);
		expect(storeGame.turnTimeRemaining).toBeGreaterThanOrEqual(9);
		expect(storeGame.turnTimeRemaining).toBeLessThanOrEqual(10);
	});

	it("a turn_advance deadline_ms of 0 does not clobber a live countdown", () => {
		const deadline = Date.now() + 10_000;
		handler("match_state_updated")(snapshot({ turn_deadline_ms: deadline }));
		const before = storeGame.turnTimeRemaining;

		handler("match_event")(
			frame(1, "turn_advance", { from: "a", to: "b", direction: 1, deadline_ms: 0 })
		);

		expect(storeGame.turnTimeRemaining).toBe(before);
	});

	it("an unarmed snapshot (turn_deadline_ms 0) does not clobber a live countdown", () => {
		const deadline = Date.now() + 10_000;
		handler("match_state_updated")(snapshot({ turn_deadline_ms: deadline }));
		const before = storeGame.turnTimeRemaining;

		handler("match_state_updated")(snapshot({ turn_deadline_ms: 0 }));

		expect(storeGame.turnTimeRemaining).toBe(before);
	});

	it("a turn_advance with a positive deadline refreshes the countdown", () => {
		handler("match_state_updated")(snapshot());

		handler("match_event")(
			frame(1, "turn_advance", {
				from: "a",
				to: "b",
				direction: 1,
				deadline_ms: Date.now() + 5_000
			})
		);

		expect(storeGame.turnTimeRemaining).toBeGreaterThanOrEqual(4);
		expect(storeGame.turnTimeRemaining).toBeLessThanOrEqual(5);
	});
});
