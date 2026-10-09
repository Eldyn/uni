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
import type { MatchEventBeat } from "$lib/stores/matchEventMap";

function handler(action: string): (payload: any) => void {
	const h = (globalThis as any).__wsTestHandlers?.[action];
	if (!h) throw new Error(`${action} handler was not registered`);
	return h;
}

function frame(seq: number, type: string, payload: unknown = {}) {
	return { action: "match_event", seq, type, payload };
}

function snapshot(seqWatermark?: number, tail?: unknown[]) {
	return {
		match_state: {
			current_player: "alice",
			active_type: "red",
			players: [{ username: "alice", card_count: 5, is_bot: false }],
			draw_pile: { count: 40 },
			...(seqWatermark === undefined ? {} : { seq_watermark: seqWatermark }),
			...(tail === undefined ? {} : { tail })
		},
		action_required: null
	};
}

const replayedEnvelope = (sequence: number, id: string, payload: object = {}) => ({
	sequence,
	seq: sequence,
	id,
	type: id,
	replayed: true,
	payload
});

describe("storeGame snapshot tail", () => {
	const unsubscribers: (() => void)[] = [];
	const onTail = (cb: (tail: any[]) => void) => {
		const off = storeGame.onTail(cb);
		unsubscribers.push(off);
		return off;
	};

	beforeEach(() => {
		storeGame.reset();
	});

	afterEach(() => {
		for (const off of unsubscribers.splice(0)) off();
	});

	it("hands the tail of the first snapshot to the play log subscribers only", () => {
		const tails: unknown[][] = [];
		const beats: MatchEventBeat[] = [];
		onTail((tail) => tails.push(tail));
		storeGame.onMatchEventBeat((beat) => beats.push(beat));

		const tail = [
			replayedEnvelope(0, "turn_skipped", { target: "bob" }),
			replayedEnvelope(1, "turn_advance", { from: "a", to: "b", direction: 1, deadline_ms: 9 })
		];
		handler("match_state_updated")(snapshot(5, tail));

		expect(tails).toHaveLength(1);
		expect(tails[0]).toHaveLength(2);
		expect(beats).toEqual([]);
	});

	it("does not move the live watermark or raise a desync", () => {
		onTail(() => undefined);
		let desyncs = 0;
		storeGame.onDesync(() => {
			desyncs += 1;
		});
		handler("match_state_updated")(snapshot(5, [replayedEnvelope(0, "turn_skipped")]));
		expect(storeGame.lastSeq).toBe(4);

		handler("match_event")(frame(5, "reshuffle", { draw_size: 10, discard_size: 2 }));
		expect(desyncs).toBe(0);
		expect(storeGame.lastSeq).toBe(5);
	});

	it("drops a replayed frame that arrives as a live match_event", () => {
		const beats: MatchEventBeat[] = [];
		storeGame.onMatchEventBeat((beat) => beats.push(beat));
		handler("match_state_updated")(snapshot(3));
		handler("match_event")({
			action: "match_event",
			...replayedEnvelope(0, "reshuffle", { draw_size: 10, discard_size: 2 })
		});
		handler("match_state_updated")(snapshot(3));
		expect(beats).toEqual([]);
		expect(storeGame.lastSeq).toBe(2);
		expect(storeGame.desynced).toBe(false);
	});

	it("replays the tail once per match, not again after a gap snapshot", () => {
		const tails: unknown[][] = [];
		onTail((tail) => tails.push(tail));
		const tail = [replayedEnvelope(0, "turn_skipped", { target: "bob" })];

		handler("match_state_updated")(snapshot(5, tail));
		handler("match_event")(frame(7, "reshuffle", { draw_size: 10, discard_size: 2 }));
		expect(storeGame.desynced).toBe(true);
		handler("match_state_updated")(snapshot(8, tail));

		expect(tails).toHaveLength(1);
		expect(storeGame.desynced).toBe(false);
	});

	it("replays again for a new match after reset", () => {
		const tails: unknown[][] = [];
		onTail((tail) => tails.push(tail));
		const tail = [replayedEnvelope(0, "turn_skipped", { target: "bob" })];
		handler("match_state_updated")(snapshot(5, tail));
		storeGame.reset();
		handler("match_state_updated")(snapshot(5, tail));
		expect(tails).toHaveLength(2);
	});

	it("sends nothing when the snapshot has no tail", () => {
		const tails: unknown[][] = [];
		onTail((tail) => tails.push(tail));
		handler("match_state_updated")(snapshot(5));
		expect(tails).toEqual([]);
	});

	it("hands a kept tail to a subscriber that mounts after the snapshot", () => {
		const tail = [replayedEnvelope(0, "turn_skipped", { target: "bob" })];
		handler("match_state_updated")(snapshot(5, tail));

		const tails: unknown[][] = [];
		onTail((received) => tails.push(received));
		expect(tails).toHaveLength(1);
		expect(tails[0]).toHaveLength(1);

		onTail((received) => tails.push(received));
		expect(tails).toHaveLength(1);
	});

	it("forgets a kept tail on reset", () => {
		handler("match_state_updated")(snapshot(5, [replayedEnvelope(0, "turn_skipped")]));
		storeGame.reset();
		const tails: unknown[][] = [];
		onTail((received) => tails.push(received));
		expect(tails).toEqual([]);
	});
});
