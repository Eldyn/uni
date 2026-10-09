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
import { bindingRegistry } from "$lib/presentation/eventBindings";
import { clientEventOfBeat, type MatchEventBeat } from "$lib/stores/matchEventMap";

function handler(action: string): (payload: any) => void {
	const h = (globalThis as any).__wsTestHandlers?.[action];
	if (!h) throw new Error(`${action} handler was not registered`);
	return h;
}

function frame(seq: number, type: string, payload: unknown = {}, extra: object = {}) {
	return { action: "match_event", seq, type, payload, ...extra };
}

const binding = (event: string, id: string) => ({
	id,
	def_hash: "0123456789abcdef",
	"uni:format_version": "1.0.0",
	"uni:binding": {
		"uni:event": event,
		"uni:cases": [{ "uni:present": { "uni:log": { "uni:variants": ["k"] } } }]
	}
});

const defs = (presentations: unknown[]) => ({
	defs_digest: "d1",
	mods: [{ id: "test", version: "1.0.0", index: 0 }],
	kinds: [],
	presentations
});

const snapshot = {
	match_state: {
		current_player: "alice",
		active_type: "red",
		players: [{ username: "alice", card_count: 1, is_bot: false, hand: [] }],
		draw_pile: { count: 40 },
		discard_pile: { count: 1, top: { card: 12345, kind: "uni:red_5", color: "red", value: "5" } },
		seq_watermark: 9
	},
	action_required: null
};

function collectBeats(): MatchEventBeat[] {
	const beats: MatchEventBeat[] = [];
	storeGame.onMatchEventBeat((beat) => beats.push(beat));
	return beats;
}

describe("storeGame feeds the presentation bindings", () => {
	beforeEach(() => {
		storeGame.reset();
		storeCardDefs.reset();
		bindingRegistry.clear();
	});

	it("fills the binding registry from a defs frame", () => {
		handler("match_event")(
			frame(1, "defs", defs([binding("test:apple_eaten", "test:shown"), { id: "t:lsd" }]))
		);
		expect(bindingRegistry.forEvent("test:apple_eaten").map((b) => b.id)).toEqual(["test:shown"]);
	});

	it("replaces the bindings when a new defs frame comes", () => {
		handler("match_event")(frame(1, "defs", defs([binding("test:apple_eaten", "test:shown")])));
		handler("match_event")(frame(2, "defs", defs([])));
		expect(bindingRegistry.size).toBe(0);
	});

	it("drops the bindings on a reset", () => {
		handler("match_event")(frame(1, "defs", defs([binding("test:apple_eaten", "test:shown")])));
		storeGame.reset();
		expect(bindingRegistry.size).toBe(0);
	});

	it("gives a mod event beat its client event", () => {
		const beats = collectBeats();
		handler("match_event")(frame(1, "defs", defs([])));
		handler("match_event")(
			frame(
				2,
				"mod_event",
				{ target: "alice", count: 3 },
				{ id: "test:apple_eaten", actor: "alice", sequence: 4 }
			)
		);
		handler("match_state_updated")(snapshot);

		const beat = beats.find((candidate) => candidate.kind === "ignored");
		expect(beat).toBeDefined();
		expect(clientEventOfBeat(beat!)).toEqual({
			id: "test:apple_eaten",
			actor: "alice",
			sequence: 4,
			payload: { target: "alice", count: 3 },
			replayed: false
		});
	});

	it("gives a bound core event that the animation map does not know an ignored beat", () => {
		const beats = collectBeats();
		handler("match_event")(frame(1, "defs", defs([binding("round_advance", "test:round")])));
		handler("match_event")(frame(2, "round_advance", { round: 2 }));
		handler("match_state_updated")(snapshot);

		const beat = beats.find((candidate) => candidate.kind === "ignored");
		expect(beat).toMatchObject({ kind: "ignored", eventId: "round_advance" });
		expect(clientEventOfBeat(beat!)?.id).toBe("round_advance");
	});

	it("makes no beat for an unknown core event that has no binding", () => {
		const beats = collectBeats();
		handler("match_event")(frame(1, "defs", defs([])));
		handler("match_event")(frame(2, "round_advance", { round: 2 }));
		handler("match_state_updated")(snapshot);

		expect(beats.filter((candidate) => candidate.kind === "ignored")).toEqual([]);
	});
});
