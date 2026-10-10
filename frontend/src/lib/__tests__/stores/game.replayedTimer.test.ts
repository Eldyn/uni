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
import { storeAuth } from "$lib/stores/auth.svelte";
import { storeLobby } from "$lib/stores/lobby.svelte";

const NOW = 1_000_000;

function handler(action: string): (payload: any) => void {
	const h = (globalThis as any).__wsTestHandlers?.[action];
	if (!h) throw new Error(`${action} handler was not registered`);
	return h;
}

const WINDOW_PAYLOAD = {
	window_id: "7",
	kind: "jump_in",
	kinds: ["jump_in"],
	hold_ms: 800,
	duration_ms: 7000,
	deadline_ms: 7000,
	targets: ["alice"],
	eligible_filter_digest: "d"
};

function liveFrame(sequence: number, id: string, payload: object) {
	return { action: "match_event", sequence, id, payload };
}

function replayedFrame(sequence: number, id: string, payload: object) {
	return { action: "match_event", sequence, id, replayed: true, payload };
}

function snapshot(tail: unknown[] | undefined) {
	return {
		match_state: {
			current_player: "bob",
			players: [
				{ username: "alice", card_count: 5, is_bot: false },
				{ username: "bob", card_count: 5, is_bot: false }
			],
			draw_pile: { count: 40 },
			seq_watermark: 3,
			...(tail === undefined ? {} : { tail })
		},
		action_required: null
	};
}

describe("a replayed client event does not restart a timer", () => {
	beforeEach(() => {
		vi.useFakeTimers();
		vi.setSystemTime(NOW);
		storeAuth.username = "alice";
		storeLobby.current = null;
		storeGame.reset();
	});

	afterEach(() => {
		vi.useRealTimers();
	});

	it("keeps the armed window when a replayed window_opened arrives live", () => {
		handler("match_event")(liveFrame(1, "window_opened", WINDOW_PAYLOAD));
		const armed = storeGame.activeTimer;
		expect(armed?.deadlineAt).toBe(NOW + 7000);

		vi.setSystemTime(NOW + 3000);
		handler("match_event")(
			replayedFrame(0, "window_opened", { ...WINDOW_PAYLOAD, deadline_ms: 9000 })
		);

		expect(storeGame.activeTimer).toEqual(armed);
	});

	it("arms no timer from a replayed window_opened and turn_advance of the tail", () => {
		storeGame.onTail(() => undefined);
		handler("match_state_updated")(
			snapshot([
				replayedFrame(0, "window_opened", WINDOW_PAYLOAD),
				replayedFrame(1, "turn_advance", {
					from: "bob",
					to: "alice",
					direction: 1,
					deadline_ms: NOW + 5000
				})
			])
		);

		expect(storeGame.activeTimer).toBeNull();
	});
});
