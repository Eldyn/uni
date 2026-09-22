import { describe, it, expect, vi, beforeEach } from "vitest";

const { emitted, auth } = vi.hoisted(() => ({
	emitted: [] as Array<{ action: string; payload: unknown }>,
	auth: { username: "alice" }
}));

// Minimal ws mock: capture outgoing frames to assert the frame the
// store emits and validate it against the real generated outgoing schema.
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
		MatchPromptResponse: "match_prompt_response",
		MatchWindowResponse: "match_window_response",
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
		on() {
			return () => {};
		},
		onOpen() {
			return () => {};
		},
		onClose() {
			return () => {};
		},
		emit(action: string, payload: unknown) {
			emitted.push({ action, payload });
		},
		emitAndWait: async () => ({ ok: false })
	}
}));

vi.mock("$lib/stores/auth.svelte", () => ({ storeAuth: auth }));

import { storeGame } from "$lib/stores/game.svelte";
import { MatchWindowResponseMessageSchema } from "$lib/generated/schemas";

const WINDOW = {
	windowId: "42",
	deadlineAt: Date.now() + 5000,
	responders: ["alice", "bob"],
	eligibleFilterDigest: "digest-1"
};

function asResponder(username: string) {
	storeGame.state = {
		active_type: "red",
		current_turn: username,
		play_direction: 1,
		players: [{ username, card_count: 5, is_bot: false }],
		pending_draws: 0,
		draw_pile_size: 40
	};
}

describe("game store: passWindow", () => {
	beforeEach(() => {
		emitted.length = 0;
		auth.username = "alice";
		storeGame.state = null;
		storeGame.activeWindow = null;
	});

	it("emits a match_window_response pass frame for a responder", () => {
		asResponder("alice");
		storeGame.activeWindow = WINDOW;

		storeGame.passWindow();

		expect(emitted).toEqual([
			{
				action: "match_window_response",
				payload: { window_id: "42", pass: true }
			}
		]);
	});

	it("produces a frame that passes the outgoing schema", () => {
		asResponder("alice");
		storeGame.activeWindow = WINDOW;

		storeGame.passWindow();

		const { action, payload } = emitted[emitted.length - 1];
		const parsed = MatchWindowResponseMessageSchema.safeParse({
			action,
			...(payload as Record<string, unknown>)
		});
		expect(parsed.success).toBe(true);
		if (parsed.success) {
			expect(parsed.data.action).toBe("match_window_response");
			expect(parsed.data.window_id).toBe("42");
			expect(parsed.data.pass).toBe(true);
		}
	});

	it("is a no-op for a non-responder", () => {
		asResponder("alice");
		storeGame.activeWindow = { ...WINDOW, responders: ["bob"] };

		storeGame.passWindow();

		expect(emitted).toEqual([]);
	});

	it("is a no-op when there is no open window", () => {
		asResponder("alice");

		storeGame.passWindow();

		expect(emitted).toEqual([]);
	});

	it("is a no-op when the local player is unknown", () => {
		storeGame.activeWindow = WINDOW;

		storeGame.passWindow();

		expect(emitted).toEqual([]);
	});
});
