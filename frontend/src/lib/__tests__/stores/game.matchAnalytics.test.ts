import { describe, it, expect, beforeEach, vi } from "vitest";

const { trackMock } = vi.hoisted(() => ({ trackMock: vi.fn() }));

vi.mock("$lib/stores/analytics.svelte", () => ({ storeAnalytics: { track: trackMock } }));

vi.mock("$lib/stores/auth.svelte", () => ({ storeAuth: { username: "alice" } }));

vi.mock("$lib/stores/navigation.svelte", () => ({
	storeNavigation: { current: "game", initialScreen: null, goto: vi.fn() }
}));

vi.mock("$lib/stores/lobby.svelte", () => ({
	storeLobby: {
		current: {
			host: "alice",
			settings: { active_mods: [], bot_count: 0, starting_cards: 7 },
			members: []
		},
		matchKey: "test-match-key"
	}
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
			if (!(globalThis as any).__wsTestHandlers) (globalThis as any).__wsTestHandlers = {};
			(globalThis as any).__wsTestHandlers[action] = handler;
			return () => {};
		},
		onOpen() {
			return () => {};
		},
		onClose(handler: any) {
			if (!(globalThis as any).__wsCloseHandlers) (globalThis as any).__wsCloseHandlers = [];
			(globalThis as any).__wsCloseHandlers.push(handler);
			return () => {};
		},
		emit() {},
		emitAndWait: async () => ({ ok: false })
	}
}));

import { storeGame } from "$lib/stores/game.svelte";
import { storeLobby } from "$lib/stores/lobby.svelte";

function handler(action: string): (payload: any) => void {
	const h = (globalThis as any).__wsTestHandlers?.[action];
	if (!h) throw new Error(`${action} handler was not registered`);
	return h;
}

function frame(seq: number, type: string, payload: unknown = {}) {
	return { action: "match_event", seq, type, payload };
}

function snapshot(currentPlayer: string, extra: Record<string, unknown> = {}) {
	return {
		match_state: {
			current_player: currentPlayer,
			active_type: "red",
			players: [
				{ username: "alice", card_count: 5, is_bot: false },
				{ username: "bob", card_count: 5, is_bot: false }
			],
			draw_pile: { count: 40 },
			discard_pile: { count: 3 },
			...extra
		},
		action_required: null
	};
}

function callsFor(event: string) {
	return trackMock.mock.calls.filter(([name]) => name === event).map(([, params]) => params);
}

describe("storeGame match analytics", () => {
	beforeEach(() => {
		storeGame.reset();
		trackMock.mockClear();
		(storeLobby as any).current.host = "alice";
	});

	it("emits match_end with match_key and depth counters", () => {
		handler("match_state_updated")(snapshot("alice"));
		handler("match_state_updated")(snapshot("bob"));
		handler("match_state_updated")(snapshot("alice"));

		handler("match_event")(
			frame(1, "card_played", { player: "alice", card: 1, from_zone_ordinal: 0 })
		);
		handler("match_event")(
			frame(2, "card_played", { player: "bob", card: 2, from_zone_ordinal: 0 })
		);
		handler("match_event")(
			frame(3, "auto_played", { player: "bob", card: 3, trigger_summary: "auto" })
		);
		handler("match_event")(
			frame(4, "cards_drawn", { player: "alice", count: 2, source_pile: "draw", cards: [4, 5] })
		);

		handler("match_over")({ winner: "alice" });

		const [params] = callsFor("match_end");
		expect(params.match_key).toBe("test-match-key");
		expect(params.turn_count).toBe(2);
		expect(params.human_turn_count).toBe(2);
		expect(params.cards_played).toBe(3);
		expect(params.auto_plays).toBe(1);
		expect(params.cards_drawn).toBe(2);
		expect(params.winner_is_bot).toBe(false);
	});

	it("emits match_abandoned (not match_end/match_saved) with no winner and no reason", () => {
		handler("match_state_updated")(snapshot("alice"));
		handler("match_over")({ winner: "" });

		expect(callsFor("match_end")).toHaveLength(0);
		expect(callsFor("match_saved")).toHaveLength(0);

		const [params] = callsFor("match_abandoned");
		expect(params.player_count).toBe(2);
		expect(params.human_count).toBe(2);
		expect(params.host_disconnects).toBe(0);
		expect(params.match_key).toBe("test-match-key");
	});

	it("counts host disconnects during a live match", () => {
		handler("match_state_updated")(snapshot("alice"));

		for (const h of (globalThis as any).__wsCloseHandlers ?? []) h();
		for (const h of (globalThis as any).__wsCloseHandlers ?? []) h();

		handler("match_over")({ winner: "" });

		const [params] = callsFor("match_abandoned");
		expect(params.host_disconnects).toBe(2);
	});

	it("emits match_saved when a reason is present but there is no winner", () => {
		handler("match_state_updated")(snapshot("alice"));
		handler("match_over")({ winner: "", reason: "quit_saved" });

		expect(callsFor("match_abandoned")).toHaveLength(0);
		const [params] = callsFor("match_saved");
		expect(params.match_key).toBe("test-match-key");
	});

	it("emits nothing from a non-host client", () => {
		(storeLobby as any).current.host = "carol";

		handler("match_state_updated")(snapshot("alice"));
		handler("match_over")({ winner: "alice" });

		expect(trackMock).not.toHaveBeenCalled();
	});
});
