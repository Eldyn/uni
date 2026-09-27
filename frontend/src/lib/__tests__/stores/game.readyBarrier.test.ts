import { describe, it, expect, vi, beforeEach } from "vitest";

const { handlers, wsMock } = vi.hoisted(() => ({
	handlers: {} as Record<string, (data: Record<string, unknown>) => void>,
	wsMock: { emit: vi.fn() }
}));

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
		MatchClientReady: "match_client_ready"
	},
	ws: {
		on(action: string, handler: (data: Record<string, unknown>) => void) {
			handlers[action] = handler;
			return () => {};
		},
		onOpen: () => () => {},
		onClose: () => () => {},
		emit: wsMock.emit,
		emitAndWait: async () => ({ ok: false })
	}
}));

import { storeGame, type GameState } from "$lib/stores/game.svelte";

const MATCH_START_PAYLOAD = {
	defs_digest: "digest-1",
	mods: [],
	deck_id: "vanilla",
	deck_name: "Vanilla",
	settings: {}
};

const FIXTURE_STATE: GameState = {
	active_type: "white",
	current_turn: "alice",
	play_direction: 1,
	players: [],
	pending_draws: 0,
	draw_pile_size: 0
};

let seq = 0;

function pushMatchEvent(type: string, payload: unknown = {}) {
	handlers["match_event"]({ seq: ++seq, type, payload });
}

describe("storeGame ready barrier", () => {
	beforeEach(() => {
		seq = 0;
		storeGame.reset();
		wsMock.emit.mockClear();
	});

	it("match_start resets barrier state", () => {
		storeGame.readyProgress = { ready: 1, total: 2 };
		storeGame.matchBegun = true;
		pushMatchEvent("match_start", MATCH_START_PAYLOAD);
		expect(storeGame.readyProgress).toBeNull();
		expect(storeGame.matchBegun).toBe(false);
	});

	it("players_ready sets readyProgress", () => {
		pushMatchEvent("match_start", MATCH_START_PAYLOAD);
		pushMatchEvent("players_ready", { ready: 2, total: 4 });
		expect(storeGame.readyProgress).toEqual({ ready: 2, total: 4 });
	});

	it("ignores a malformed players_ready payload", () => {
		pushMatchEvent("match_start", MATCH_START_PAYLOAD);
		pushMatchEvent("players_ready", { ready: "x" });
		expect(storeGame.readyProgress).toBeNull();
	});

	it("match_begin after snapshot sets matchBegun", () => {
		pushMatchEvent("match_begin", {});
		expect(storeGame.matchBegun).toBe(true);
	});

	it("sendClientReady emits once per match", () => {
		pushMatchEvent("match_start", MATCH_START_PAYLOAD);
		storeGame.sendClientReady();
		storeGame.sendClientReady();
		expect(wsMock.emit).toHaveBeenCalledTimes(1);
		expect(wsMock.emit).toHaveBeenCalledWith("match_client_ready");
		pushMatchEvent("match_start", MATCH_START_PAYLOAD);
		storeGame.sendClientReady();
		expect(wsMock.emit).toHaveBeenCalledTimes(2);
	});

	it("introReady requires matchBegun", () => {
		storeGame.state = FIXTURE_STATE;
		storeGame.matchIntroPending = true;
		storeGame.matchBegun = false;
		expect(storeGame.introReady).toBe(false);
		storeGame.matchBegun = true;
		expect(storeGame.introReady).toBe(true);
	});
});
