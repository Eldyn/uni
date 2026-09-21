import { describe, it, expect, beforeEach, vi } from "vitest";

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
import { storeCardDefs } from "$lib/stores/cardDefs.svelte";

function handler(action: string): (payload: any) => void {
	const h = (globalThis as any).__wsTestHandlers?.[action];
	if (!h) throw new Error(`${action} handler was not registered`);
	return h;
}

function frame(seq: number, type: string, payload: unknown = {}) {
	return { action: "match_event", seq, type, payload };
}

function defsPayload(digest: string) {
	return {
		defs_digest: digest,
		mods: [{ id: "vanilla", version: "1.0.0", index: 0 }],
		kinds: [
			{
				index: 0,
				string_id: "vanilla:red_5",
				face: { kind: "text", color: "red", label: "5", art_version: 1 },
				tags: ["colored", "numbered"]
			}
		]
	};
}

function matchStart(digest: string) {
	return {
		defs_digest: digest,
		mods: [],
		deck_id: "vanilla:classic",
		deck_name: "Classic",
		settings: {}
	};
}

function snapshotWithKind(kind: string) {
	return {
		match_state: {
			current_player: "alice",
			active_type: "red",
			players: [
				{
					username: "alice",
					card_count: 1,
					is_bot: false,
					hand: [{ card: 12345, kind, color: "red", value: "5", can_play: true }]
				}
			],
			draw_pile: { count: 40 },
			discard_pile: { count: 1, top: { card: 12345, kind, color: "red", value: "5" } },
			seq_watermark: 4
		},
		action_required: null
	};
}

describe("storeGame consumes defs + match_start", () => {
	beforeEach(() => {
		storeGame.reset();
		storeCardDefs.reset();
	});

	it("populates the kind table from a defs frame through the match_event owner", () => {
		handler("match_event")(frame(1, "defs", defsPayload("d1")));

		expect(storeCardDefs.lookupByStringId("vanilla:red_5")?.face.label).toBe("5");
		expect(storeCardDefs.lookupByIndex(0, 0)?.string_id).toBe("vanilla:red_5");
	});

	it("keeps kind on the snapshot Card and exposes the resolved face", () => {
		handler("match_event")(frame(1, "defs", defsPayload("d1")));
		handler("match_event")(frame(2, "match_start", matchStart("d1")));
		handler("match_state_updated")(snapshotWithKind("vanilla:red_5"));

		const top = storeGame.state?.top_card;
		expect(top?.kind).toBe("vanilla:red_5");
		expect(top?.face?.label).toBe("5");
		expect(top?.face?.color).toBe("red");

		const handCard = storeGame.state?.players[0]?.hand?.[0];
		expect(handCard?.kind).toBe("vanilla:red_5");
		expect(handCard?.face?.label).toBe("5");
	});

	it("still parses a card whose kind has no defs entry (vanilla fallback)", () => {
		handler("match_event")(frame(1, "defs", defsPayload("d1")));
		handler("match_event")(frame(2, "match_start", matchStart("d1")));
		handler("match_state_updated")(snapshotWithKind("vanilla:mystery"));

		const top = storeGame.state?.top_card;
		expect(top?.kind).toBe("vanilla:mystery");
		expect(top?.face).toBeUndefined();
		expect(top?.type).toBe("red");
		expect(top?.value).toBe("5");
	});

	it("clears the defs table on returnToLobby", () => {
		handler("match_event")(frame(1, "defs", defsPayload("d1")));
		expect(storeCardDefs.lookupByStringId("vanilla:red_5")).toBeDefined();

		storeGame.returnToLobby();
		expect(storeCardDefs.lookupByStringId("vanilla:red_5")).toBeUndefined();
	});
});
