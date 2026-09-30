import { describe, it, expect, beforeEach, afterEach, vi } from "vitest";

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

function snapshot(matchState: Record<string, unknown> = {}) {
	return {
		match_state: {
			current_player: "alice",
			active_type: "red",
			players: [{ username: "alice", card_count: 5, is_bot: false }],
			draw_pile: { count: 40 },
			...matchState
		},
		action_required: null
	};
}

// INFO: `window_open.deadline_ms` is a REMAINING duration; the snapshot window
// deadline and `turn_advance.deadline_ms` are ABSOLUTE epoch-ms values.
const WINDOW_OPEN = {
	window_id: "42",
	deadline_ms: 5000,
	responders: ["alice", "bob"],
	eligible_filter_digest: "digest-1"
};

const PROMPT_OPEN = {
	prompt_id: "choose_color",
	kind: "choose_color",
	payload: { options: ["red", "blue"] },
	response_schema: { type: "string", enum: ["red", "blue"] },
	duration_ms: 15000,
	deadline_ms: 0
};

describe("storeGame turn/window/prompt state", () => {
	beforeEach(() => {
		vi.useFakeTimers();
		storeGame.reset();
	});

	afterEach(() => {
		vi.useRealTimers();
	});

	it("normalizes the absolute turn_advance deadline and ticks it down", () => {
		const now = Date.now();
		handler("match_event")(
			frame(1, "turn_advance", { from: "a", to: "b", direction: 1, deadline_ms: now + 7000 })
		);

		expect(storeGame.turnTimeRemaining).toBe(7);
		vi.advanceTimersByTime(1000);
		expect(storeGame.turnTimeRemaining).toBe(6);
	});

	it("normalizes the remaining window_open deadline to an absolute deadline and counts down", () => {
		const now = Date.now();
		handler("match_event")(frame(1, "window_open", WINDOW_OPEN));

		expect(storeGame.activeWindow).toEqual({
			windowId: "42",
			deadlineAt: now + 5000,
			durationMs: 5000,
			holdMs: 0,
			kinds: [],
			responders: ["alice", "bob"],
			eligibleFilterDigest: "digest-1"
		});
		expect(storeGame.activeTimer?.deadlineAt).toBe(now + 5000);
	});

	it("closes the response window on window_close", () => {
		const onEvent = handler("match_event");
		onEvent(frame(1, "window_open", WINDOW_OPEN));
		onEvent(frame(2, "window_close", { outcome: "passed" }));

		expect(storeGame.activeWindow).toBeNull();
	});

	it("sets the active prompt on prompt_open and clears it only on a matching prompt_close", () => {
		const onEvent = handler("match_event");
		onEvent(frame(1, "prompt_open", PROMPT_OPEN));
		expect(storeGame.activePrompt?.prompt_id).toBe("choose_color");

		onEvent(frame(2, "prompt_close", { prompt_id: "choose_value", outcome: "answered" }));
		expect(storeGame.activePrompt?.prompt_id).toBe("choose_color");

		onEvent(frame(3, "prompt_close", { prompt_id: "choose_color", outcome: "answered" }));
		expect(storeGame.activePrompt).toBeNull();
	});

	it("hydrates the absolute snapshot window deadline and prompt", () => {
		const now = Date.now();
		handler("match_state_updated")(
			snapshot({
				window: {
					window_id: "9",
					deadline_ms: now + 3000,
					responders: ["alice"],
					eligible_filter_digest: "digest-9",
					responses: []
				},
				prompts: [
					{
						prompt_id: "choose_player",
						kind: "choose_player",
						payload: { candidates: ["bob"] },
						response_schema: { type: "string" },
						deadline_ms: 0
					}
				]
			})
		);

		expect(storeGame.activeWindow).toEqual({
			windowId: "9",
			deadlineAt: now + 3000,
			durationMs: 3000,
			holdMs: 0,
			kinds: [],
			responders: ["alice"],
			eligibleFilterDigest: "digest-9"
		});
		expect(storeGame.activePrompt?.prompt_id).toBe("choose_player");
	});

	it("clears window and prompt when the snapshot reports none", () => {
		const onEvent = handler("match_event");
		onEvent(frame(1, "window_open", WINDOW_OPEN));
		onEvent(frame(2, "prompt_open", PROMPT_OPEN));

		handler("match_state_updated")(snapshot({ window: null, prompts: [] }));

		expect(storeGame.activeWindow).toBeNull();
		expect(storeGame.activePrompt).toBeNull();
	});

	it("clears window and prompt on reset", () => {
		const onEvent = handler("match_event");
		onEvent(frame(1, "window_open", WINDOW_OPEN));
		onEvent(frame(2, "prompt_open", PROMPT_OPEN));

		storeGame.reset();

		expect(storeGame.activeWindow).toBeNull();
		expect(storeGame.activePrompt).toBeNull();
	});

	it("clears window and prompt on returnToLobby", () => {
		const onEvent = handler("match_event");
		onEvent(frame(1, "window_open", WINDOW_OPEN));
		onEvent(frame(2, "prompt_open", PROMPT_OPEN));

		storeGame.returnToLobby();

		expect(storeGame.activeWindow).toBeNull();
		expect(storeGame.activePrompt).toBeNull();
	});
});
