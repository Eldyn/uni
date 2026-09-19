import { describe, it, expect, beforeEach, afterEach, vi } from "vitest";

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
			if (!(globalThis as any).__wsBroadcastHandlers) {
				(globalThis as any).__wsBroadcastHandlers = {};
			}
			(globalThis as any).__wsBroadcastHandlers[action] = handler;
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

import { render, act } from "@testing-library/svelte";
import TurnOrderStrip from "$components/game/TurnOrderStrip.svelte";
import { storeGame } from "$lib/stores/game.svelte";

function broadcast(usernames: string[], currentTurn: string, playDirection = 1) {
	const handler = (globalThis as any).__wsBroadcastHandlers?.["match_state_updated"];
	if (!handler) throw new Error("MatchStateUpdated handler not registered");
	handler({
		match_state: {
			active_type: 0,
			current_turn: currentTurn,
			play_direction: playDirection,
			players: usernames.map((username, i) => ({
				username,
				card_count: 5,
				is_bot: i % 2 === 1
			})),
			draw_pile_size: 40
		},
		action_required: null
	});
}

function chipLabels(): string[] {
	return Array.from(document.querySelectorAll(".chip")).map(
		(el) => el.getAttribute("aria-label") ?? ""
	);
}

describe("TurnOrderStrip tracks real MatchStateUpdated broadcasts", () => {
	beforeEach(() => {
		storeGame.state = null;
	});

	afterEach(() => {
		storeGame.state = null;
		document.body.innerHTML = "";
	});

	it("updates across normal advance, skip, reverse and draw over the wire", async () => {
		broadcast(["a", "b", "c", "d"], "a", 1);
		render(TurnOrderStrip);
		expect(chipLabels()).toContain("a");

		const sequence: Array<[string, number]> = [
			["b", 1],
			["d", 1],
			["d", -1],
			["b", -1],
			["a", -1],
			["c", -1]
		];
		for (const [turn, dir] of sequence) {
			await act(() => {
				broadcast(["a", "b", "c", "d"], turn, dir);
			});
			const labels = chipLabels();
			expect(labels).toContain(turn);
			expect(document.querySelector(".chip.current")?.getAttribute("aria-label")).toBe(turn);
		}
	});

	it("shows the eliminated-player-free window after the roster shrinks", async () => {
		broadcast(["a", "b", "c", "d"], "a", 1);
		render(TurnOrderStrip);
		await act(() => {
			broadcast(["a", "b", "c"], "a", 1);
		});
		expect(chipLabels().some((l) => l === "d")).toBe(false);
	});
});
