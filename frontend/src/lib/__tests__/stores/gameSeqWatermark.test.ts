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
import type { MatchEventBeat } from "$lib/stores/matchEventMap";

function handler(action: string): (payload: any) => void {
	const h = (globalThis as any).__wsTestHandlers?.[action];
	if (!h) throw new Error(`${action} handler was not registered`);
	return h;
}

function frame(seq: number, type: string, payload: unknown = {}) {
	return { action: "match_event", seq, type, payload };
}

function snapshot(seqWatermark?: number) {
	return {
		match_state: {
			current_player: "alice",
			active_type: "red",
			players: [{ username: "alice", card_count: 5, is_bot: false }],
			draw_pile: { count: 40 },
			...(seqWatermark === undefined ? {} : { seq_watermark: seqWatermark })
		},
		action_required: null
	};
}

describe("storeGame match_event seq watermark + beat buffer", () => {
	beforeEach(() => {
		storeGame.reset();
	});

	it("advances the watermark and drains buffered beats after the next snapshot", () => {
		const beats: MatchEventBeat[] = [];
		storeGame.onMatchEventBeat((b) => beats.push(b));

		const onEvent = handler("match_event");
		onEvent(frame(1, "turn_advance", { from: "a", to: "b", direction: 1, deadline_ms: 5000 }));
		onEvent(
			frame(2, "cards_drawn", { player: "alice", count: 2, source_pile: "draw", cards: [7, 8] })
		);
		onEvent(frame(3, "reshuffle", { draw_size: 10, discard_size: 2 }));

		expect(storeGame.desynced).toBe(false);
		expect(beats).toHaveLength(0);

		handler("match_state_updated")(snapshot(3));

		expect(storeGame.lastSeq).toBe(3);
		expect(beats.map((b) => b.kind)).toEqual(["turn", "draw", "reshuffle"]);
	});

	it("detects a seq gap and reconciles on a snapshot at the watermark", () => {
		let desyncs = 0;
		storeGame.onDesync(() => {
			desyncs += 1;
		});

		const onEvent = handler("match_event");
		onEvent(frame(1, "turn_advance", { from: "a", to: "b", direction: 1, deadline_ms: 5000 }));
		expect(desyncs).toBe(0);
		expect(storeGame.lastSeq).toBe(1);

		onEvent(frame(3, "reshuffle", { draw_size: 10, discard_size: 2 }));
		expect(desyncs).toBe(1);
		expect(storeGame.desynced).toBe(true);
		expect(storeGame.lastSeq).toBe(3);

		handler("match_state_updated")(snapshot(3));
		expect(storeGame.desynced).toBe(false);
		expect(storeGame.lastSeq).toBe(3);
	});

	it("advances the watermark but buffers no beat for an unknown type", () => {
		const beats: MatchEventBeat[] = [];
		storeGame.onMatchEventBeat((b) => beats.push(b));

		handler("match_event")(frame(1, "some_future_event", { anything: true }));
		expect(storeGame.lastSeq).toBe(1);

		handler("match_state_updated")(snapshot(1));
		expect(beats).toHaveLength(0);
	});

	it("clears the watermark on reset and returnToLobby", () => {
		const onEvent = handler("match_event");
		onEvent(frame(1, "reshuffle", { draw_size: 10, discard_size: 2 }));
		expect(storeGame.lastSeq).toBe(1);

		storeGame.reset();
		expect(storeGame.lastSeq).toBeNull();
		expect(storeGame.desynced).toBe(false);

		onEvent(frame(1, "reshuffle", { draw_size: 10, discard_size: 2 }));
		expect(storeGame.lastSeq).toBe(1);

		storeGame.returnToLobby();
		expect(storeGame.lastSeq).toBeNull();
		expect(storeGame.desynced).toBe(false);
	});
});
