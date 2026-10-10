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
import { storeModGlossary } from "$lib/stores/modGlossary.svelte";

function handler(action: string): (payload: any) => void {
	const h = (globalThis as any).__wsTestHandlers?.[action];
	if (!h) throw new Error(`${action} handler was not registered`);
	return h;
}

const defsWithGlossary = (glossary: unknown[]) => ({
	defs_digest: "d1",
	mods: [{ id: "my_mod", version: "1.0.0", index: 0, glossary }],
	kinds: []
});

const entry = {
	id: "my_mod:foo",
	label_key: "my_mod:foo.label",
	description_key: "my_mod:foo.description",
	source: "card"
};

describe("storeGame feeds the mod glossary", () => {
	beforeEach(() => {
		storeGame.reset();
	});

	it("fills the mod glossary from a defs frame", () => {
		handler("match_event")({
			action: "match_event",
			seq: 1,
			type: "defs",
			payload: defsWithGlossary([entry])
		});
		expect(storeModGlossary.has("my_mod:foo")).toBe(true);
	});

	it("drops the mod glossary on a reset", () => {
		handler("match_event")({
			action: "match_event",
			seq: 1,
			type: "defs",
			payload: defsWithGlossary([entry])
		});
		storeGame.reset();
		expect(storeModGlossary.has("my_mod:foo")).toBe(false);
	});
});
