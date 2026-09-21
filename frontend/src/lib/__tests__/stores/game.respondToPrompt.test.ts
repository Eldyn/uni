import { describe, it, expect, vi, beforeEach } from "vitest";

const { emitted } = vi.hoisted(() => ({
	emitted: [] as Array<{ action: string; payload: unknown }>
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

import { storeGame } from "$lib/stores/game.svelte";
import { MatchPromptResponseMessageSchema } from "$lib/generated/schemas";

function lastFrame() {
	const { action, payload } = emitted[emitted.length - 1];
	return { action, ...(payload as Record<string, unknown>) };
}

describe("game store: respondToPrompt", () => {
	beforeEach(() => {
		emitted.length = 0;
	});

	it("emits a match_prompt_response frame carrying the raw value", () => {
		storeGame.respondToPrompt("choose_color", "red");

		expect(emitted).toEqual([
			{
				action: "match_prompt_response",
				payload: { prompt_id: "choose_color", value: "red" }
			}
		]);
	});

	it("produces a string answer that passes the outgoing schema (not dropped)", () => {
		storeGame.respondToPrompt("choose_color", "red");

		const parsed = MatchPromptResponseMessageSchema.safeParse(lastFrame());
		expect(parsed.success).toBe(true);
		if (parsed.success) {
			expect(parsed.data.prompt_id).toBe("choose_color");
			expect(parsed.data.value).toBe("red");
		}
	});

	it("forwards non-string answers unchanged", () => {
		storeGame.respondToPrompt("choose_card", 42);

		const parsed = MatchPromptResponseMessageSchema.safeParse(lastFrame());
		expect(parsed.success).toBe(true);
		expect((emitted[emitted.length - 1].payload as { value: unknown }).value).toBe(42);
	});
});
