import { describe, it, expect, beforeEach, afterEach, vi } from "vitest";

// INFO: `storeGame` imports the real ws singleton; the fan-out test below drives
//       raw `match_event` frames through the mocked transport exactly like
//       `stores/gameSeqWatermark.test.ts` does.
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

import { AfflictionManager } from "$components/game/affliction/afflictionManager";
import { kindTarget } from "$components/game/affliction/compose";
import {
	evalWhere,
	PresentationTriggers,
	wirePresentationTriggers
} from "$components/game/affliction/triggers";
import type { PresentationDef } from "$components/game/affliction/triggers";
import { storeGame } from "$lib/stores/game.svelte";

function lsd(overrides: Partial<PresentationDef> = {}): PresentationDef {
	return {
		id: "test_mod:lsd",
		target: "hand",
		priority: 10,
		modifiers: [{ kind: "face.override", source: "seeded-random-tile" }],
		...overrides
	};
}

function isActive(mgr: AfflictionManager, target = "hand"): boolean {
	return mgr.resolve(kindTarget("face.override", target)) !== undefined;
}

describe("evalWhere: flat scalar equality", () => {
	it("is false when a scalar field differs", () => {
		expect(evalWhere({ reason: "unplayable" }, { reason: "playable" })).toBe(false);
	});

	it("is true on exact scalar equality", () => {
		expect(evalWhere({ reason: "unplayable" }, { reason: "unplayable" })).toBe(true);
	});

	it("treats an empty/absent where as unconditional", () => {
		expect(evalWhere({}, { anything: 1 })).toBe(true);
		expect(evalWhere(undefined as unknown as Record<string, unknown>, {})).toBe(true);
	});

	it("is false when the payload is missing the key", () => {
		expect(evalWhere({ reason: "unplayable" }, {})).toBe(false);
	});

	it("ignores extra payload fields", () => {
		expect(evalWhere({ reason: "unplayable" }, { reason: "unplayable", extra: true })).toBe(true);
	});

	it("compares scalars, not structure", () => {
		expect(evalWhere({ a: { b: 1 } }, { a: { b: 1 } })).toBe(false);
	});
});

describe("PresentationTriggers", () => {
	it("installs an always trigger at registration", () => {
		const mgr = new AfflictionManager();
		const triggers = new PresentationTriggers(mgr);

		triggers.register(lsd({ triggers: [] }));

		expect(isActive(mgr)).toBe(true);
		expect(mgr.active[0]?.presentationId).toBe("test_mod:lsd");
	});

	it("onAlways installs immediately", () => {
		const mgr = new AfflictionManager();
		const triggers = new PresentationTriggers(mgr);

		triggers.onAlways(lsd());

		expect(isActive(mgr)).toBe(true);
	});

	it("keeps a persistent trigger active (no duration)", () => {
		vi.useFakeTimers();
		try {
			const mgr = new AfflictionManager();
			const triggers = new PresentationTriggers(mgr);
			triggers.register(lsd({ triggers: [] }));

			vi.advanceTimersByTime(60_000);

			expect(isActive(mgr)).toBe(true);
		} finally {
			vi.useRealTimers();
		}
	});

	it("installs a match: trigger only when where matches", () => {
		const mgr = new AfflictionManager();
		const triggers = new PresentationTriggers(mgr);
		triggers.register(
			lsd({
				triggers: [{ on: "play_rejected", where: { reason_id: "unplayable" } }]
			})
		);

		triggers.onEvent("play_rejected", { reason_id: "playable" });
		expect(isActive(mgr)).toBe(false);

		triggers.onEvent("play_rejected", { reason_id: "unplayable" });
		expect(isActive(mgr)).toBe(true);
	});

	it("accepts the internal match: source prefix", () => {
		const mgr = new AfflictionManager();
		const triggers = new PresentationTriggers(mgr);
		triggers.register(lsd({ triggers: [{ on: "match:play_rejected" }] }));

		triggers.onEvent("play_rejected", {});
		expect(isActive(mgr)).toBe(true);
	});

	it("registers a status: trigger and installs it on that status", () => {
		const mgr = new AfflictionManager();
		const triggers = new PresentationTriggers(mgr);
		triggers.register(lsd({ triggers: [{ on: "status:skipped" }] }));

		triggers.onEvent("status_applied", { status_kind: "frozen" });
		expect(isActive(mgr)).toBe(false);

		triggers.onEvent("status_applied", { status_kind: "skipped" });
		expect(isActive(mgr)).toBe(true);
	});

	it("ignores unknown event types without throwing", () => {
		const mgr = new AfflictionManager();
		const triggers = new PresentationTriggers(mgr);

		expect(() => triggers.onEvent("some_future_event", { anything: true })).not.toThrow();
		expect(isActive(mgr)).toBe(false);
	});

	it("schedules expiry after duration seconds", () => {
		vi.useFakeTimers();
		try {
			const mgr = new AfflictionManager();
			const triggers = new PresentationTriggers(mgr);
			triggers.register(
				lsd({
					triggers: [{ on: "play_rejected", where: { reason_id: "unplayable" }, duration: 5 }]
				})
			);

			triggers.onEvent("play_rejected", { reason_id: "unplayable" });
			expect(isActive(mgr)).toBe(true);

			vi.advanceTimersByTime(4_999);
			expect(isActive(mgr)).toBe(true);

			vi.advanceTimersByTime(1);
			expect(isActive(mgr)).toBe(false);
		} finally {
			vi.useRealTimers();
		}
	});

	it("suppresses installs when disabled (reduced motion / animation off)", () => {
		const mgr = new AfflictionManager();
		const triggers = new PresentationTriggers(mgr);
		triggers.enabled = false;

		triggers.register(lsd({ triggers: [] }));
		expect(isActive(mgr)).toBe(false);

		triggers.onAlways(lsd({ id: "test_mod:other" }));
		expect(mgr.active).toHaveLength(0);
	});

	it("wirePresentationTriggers forwards raw events into onEvent", () => {
		const mgr = new AfflictionManager();
		const triggers = new PresentationTriggers(mgr);
		triggers.register(
			lsd({ triggers: [{ on: "play_rejected", where: { reason_id: "unplayable" } }] })
		);

		let emit: ((type: string, payload: unknown) => void) | undefined;
		const dispose = wirePresentationTriggers(triggers, (cb) => {
			emit = cb;
			return () => {};
		});

		emit?.("play_rejected", { reason_id: "unplayable" });
		expect(isActive(mgr)).toBe(true);

		dispose();
	});
});

describe("storeGame raw match_event fan-out", () => {
	beforeEach(() => {
		storeGame.reset();
	});

	afterEach(() => {
		(globalThis as any).__wsTestHandlers = undefined;
	});

	it("forwards the raw type and payload to onMatchEvent subscribers", () => {
		const seen: Array<[string, unknown]> = [];
		storeGame.onMatchEvent((type, payload) => seen.push([type, payload]));

		const onEvent = (globalThis as any).__wsTestHandlers["match_event"] as (p: unknown) => void;
		onEvent({
			action: "match_event",
			sequence: 1,
			id: "play_rejected",
			payload: { reason_id: "unplayable" }
		});

		expect(seen).toEqual([["play_rejected", { reason_id: "unplayable" }]]);
	});
});
