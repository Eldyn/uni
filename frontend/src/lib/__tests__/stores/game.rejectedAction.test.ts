import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";

const { handlers } = vi.hoisted(() => ({
	handlers: {} as Record<string, (data: Record<string, unknown>) => void>
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
		MatchDrawCard: "match_draw_card"
	},
	ws: {
		on(action: string, handler: (data: Record<string, unknown>) => void) {
			handlers[action] = handler;
			return () => {};
		},
		onOpen: () => () => {},
		onClose: () => () => {},
		emit() {},
		emitAndWait: async () => ({ ok: false })
	}
}));

import { storeGame } from "$lib/stores/game.svelte";

// A rejected play (e.g. tapping a card that can't be played) comes back as a
// bare error frame with no state update. The latch must drop on it, or every
// play for the next 3 s is silently swallowed.
describe("game store: rejected action releases the pending latch", () => {
	beforeEach(() => {
		vi.useFakeTimers();
		storeGame.isActionPending = false;
	});

	afterEach(() => {
		vi.useRealTimers();
		storeGame.isActionPending = false;
	});

	it("clears isActionPending when the server rejects a play", () => {
		storeGame.playCard(1);
		expect(storeGame.isActionPending).toBe(true);

		handlers["error"]({ action: "error", code: "invalid_move" });

		expect(storeGame.isActionPending).toBe(false);
	});

	it("does not leave the stale safety timer to clear a later action", () => {
		storeGame.playCard(1);
		vi.advanceTimersByTime(2000);
		handlers["error"]({ action: "error", code: "invalid_move" });
		storeGame.playCard(2);
		vi.advanceTimersByTime(1500);

		expect(storeGame.isActionPending).toBe(true);
	});
});
