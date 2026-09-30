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
		MatchClientReady: "match_client_ready",
		MatchWindowResponse: "match_window_response"
	},
	ws: {
		on(action: string, handler: any) {
			if (!(globalThis as any).__wsTestHandlers) {
				(globalThis as any).__wsTestHandlers = {};
			}
			(globalThis as any).__wsTestHandlers[action] = handler;
			return () => {};
		},
		onOpen: () => () => {},
		onClose: () => () => {},
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

function frame(seq: number, type: string, payload: unknown = {}) {
	return { action: "match_event", seq, type, payload };
}

const DEBT_STATUS = { status_kind: "vanilla:draw_debt", magnitude: 2 };

function snapshot(fields: Record<string, unknown> = {}, statuses: unknown[] = []) {
	handler("match_state_updated")({
		match_state: {
			current_player: "bob",
			players: [
				{ username: "alice", card_count: 5, is_bot: false, statuses },
				{ username: "bob", card_count: 5, is_bot: false }
			],
			draw_pile: { count: 40 },
			...fields
		},
		action_required: null
	});
}

const HELD_WINDOW = {
	window_id: "7",
	kind: "jump_in",
	kinds: ["jump_in", "generic"],
	hold_ms: 800,
	duration_ms: 7000,
	deadline_ms: 7000,
	responders: ["alice", "carol"],
	eligible_filter_digest: "d"
};

describe("storeGame activeTimer normalisation", () => {
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

	it("is null before anything is armed", () => {
		expect(storeGame.activeTimer).toBeNull();
	});

	it("normalises a window_open REMAINING deadline onto the local clock", () => {
		handler("match_event")(frame(1, "window_open", HELD_WINDOW));

		expect(storeGame.activeTimer).toEqual({
			source: "window",
			durationMs: 7000,
			deadlineAt: NOW + 7000,
			holdMs: 800,
			kinds: ["jump_in", "generic"]
		});
	});

	it("falls back to the remaining deadline and kind for a bare window_open", () => {
		handler("match_event")(
			frame(1, "window_open", {
				window_id: "1",
				deadline_ms: 5000,
				responders: ["alice"],
				eligible_filter_digest: "d"
			})
		);

		expect(storeGame.activeTimer).toEqual({
			source: "window",
			durationMs: 5000,
			deadlineAt: NOW + 5000,
			holdMs: 0,
			kinds: []
		});
	});

	it("normalises a snapshot ABSOLUTE window deadline and restores mid-window", () => {
		snapshot({
			server_now_ms: 500_000,
			window: {
				...HELD_WINDOW,
				deadline_ms: 500_000 + 4000,
				duration_ms: 7000
			}
		});

		const timer = storeGame.activeTimer!;
		expect(timer.deadlineAt).toBe(NOW + 4000);
		expect(timer.durationMs).toBe(7000);
		const remainingFraction = (timer.deadlineAt - Date.now()) / timer.durationMs;
		expect(remainingFraction).toBeCloseTo(4 / 7, 5);
	});

	it("corrects client clock skew with server_now_ms", () => {
		snapshot({
			server_now_ms: NOW + 90_000,
			turn_deadline_ms: NOW + 90_000 + 12_000
		});

		expect(storeGame.activeTimer?.source).toBe("turn");
		expect(storeGame.activeTimer?.deadlineAt).toBe(NOW + 12_000);
	});

	it("treats an absent server_now_ms as no skew", () => {
		snapshot({ turn_deadline_ms: NOW + 12_000 });

		expect(storeGame.activeTimer?.deadlineAt).toBe(NOW + 12_000);
	});

	it("takes the turn total from the lobby limit", () => {
		storeLobby.current = { settings: { turn_time_limit_ms: 20_000 } } as any;
		snapshot({ turn_deadline_ms: NOW + 12_000 });

		expect(storeGame.activeTimer).toMatchObject({ source: "turn", durationMs: 20_000 });
	});

	it("uses the first observed remaining as the turn total without a lobby limit", () => {
		snapshot({ turn_deadline_ms: NOW + 12_000 });

		expect(storeGame.activeTimer).toMatchObject({ source: "turn", durationMs: 12_000 });
	});

	it("shows no turn line while the turn clock is suspended", () => {
		snapshot({ turn_deadline_ms: NOW + 12_000 });
		snapshot({ turn_deadline_ms: 0 });

		expect(storeGame.activeTimer).toBeNull();
	});

	it("uses the prompt_open duration_ms, never the packet deadline_ms", () => {
		handler("match_event")(
			frame(1, "prompt_open", {
				prompt_id: "p",
				kind: "choose_color",
				payload: {},
				response_schema: {},
				duration_ms: 9000,
				deadline_ms: 0
			})
		);

		expect(storeGame.activeTimer).toEqual({
			source: "prompt",
			durationMs: 9000,
			deadlineAt: NOW + 9000,
			holdMs: 0,
			kinds: []
		});
	});

	it("ignores a small declared packet deadline_ms on a prompt", () => {
		handler("match_event")(
			frame(1, "prompt_open", {
				prompt_id: "p",
				kind: "choose_color",
				payload: {},
				response_schema: {},
				duration_ms: 9000,
				deadline_ms: 15
			})
		);

		expect(storeGame.activeTimer?.deadlineAt).toBe(NOW + 9000);
	});

	it("restores a snapshot prompt from its ABSOLUTE deadline", () => {
		snapshot({
			server_now_ms: 500_000,
			prompts: [
				{
					prompt_id: "p",
					kind: "choose_color",
					payload: {},
					response_schema: {},
					duration_ms: 9000,
					deadline_ms: 500_000 + 3000
				}
			]
		});

		expect(storeGame.activeTimer).toMatchObject({
			source: "prompt",
			durationMs: 9000,
			deadlineAt: NOW + 3000
		});
	});

	it("draws no prompt line when the prompt carries no clock", () => {
		handler("match_event")(
			frame(1, "prompt_open", {
				prompt_id: "p",
				kind: "choose_color",
				payload: {},
				response_schema: {},
				deadline_ms: 0
			})
		);

		expect(storeGame.activeTimer).toBeNull();
	});

	it("draws the ready barrier from players_ready.timeout_ms until match_begin", () => {
		handler("match_event")(frame(1, "players_ready", { ready: 1, total: 3, timeout_ms: 15_000 }));

		expect(storeGame.activeTimer).toEqual({
			source: "ready",
			durationMs: 15_000,
			deadlineAt: NOW + 15_000,
			holdMs: 0,
			kinds: []
		});

		handler("match_event")(frame(2, "match_begin", { instance_id: 1 }));
		expect(storeGame.activeTimer).toBeNull();
	});

	it("keeps the barrier deadline across a later players_ready without timeout_ms", () => {
		handler("match_event")(frame(1, "players_ready", { ready: 1, total: 3, timeout_ms: 15_000 }));
		vi.advanceTimersByTime(2000);
		handler("match_event")(frame(2, "players_ready", { ready: 2, total: 3 }));

		expect(storeGame.activeTimer?.deadlineAt).toBe(NOW + 15_000);
	});

	it("prefers window over prompt over ready over turn", () => {
		snapshot({ turn_deadline_ms: NOW + 12_000 });
		expect(storeGame.activeTimer?.source).toBe("turn");

		handler("match_event")(frame(1, "players_ready", { ready: 1, total: 3, timeout_ms: 15_000 }));
		expect(storeGame.activeTimer?.source).toBe("ready");

		handler("match_event")(
			frame(2, "prompt_open", {
				prompt_id: "p",
				kind: "k",
				payload: {},
				response_schema: {},
				duration_ms: 9000,
				deadline_ms: 0
			})
		);
		expect(storeGame.activeTimer?.source).toBe("prompt");

		handler("match_event")(frame(3, "window_open", HELD_WINDOW));
		expect(storeGame.activeTimer?.source).toBe("window");

		handler("match_event")(frame(4, "window_close", { outcome: "timeout" }));
		expect(storeGame.activeTimer?.source).toBe("prompt");
	});

	it("shows the window, not the turn, while the server suspends the turn clock", () => {
		snapshot({ turn_deadline_ms: 0, window: { ...HELD_WINDOW, deadline_ms: NOW + 3000 } });

		expect(storeGame.activeTimer?.source).toBe("window");
	});

	it("clears every timer on reset", () => {
		handler("match_event")(frame(1, "window_open", HELD_WINDOW));
		snapshot({ turn_deadline_ms: NOW + 12_000, window: null });
		storeGame.reset();

		expect(storeGame.activeTimer).toBeNull();
	});
});

describe("storeGame windowActionState", () => {
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

	function openHeldWindow(responders: string[]) {
		handler("match_event")(frame(1, "window_open", { ...HELD_WINDOW, responders }));
	}

	it("is null with no open window", () => {
		snapshot({}, [DEBT_STATUS]);
		expect(storeGame.windowActionState(Date.now())).toBeNull();
	});

	it("gives the debt victim a draw action disabled until the hold elapses", () => {
		snapshot({}, [DEBT_STATUS]);
		openHeldWindow(["alice"]);

		expect(storeGame.windowActionState(NOW + 799)).toEqual({ kind: "draw", enabled: false });
		expect(storeGame.windowActionState(NOW + 800)).toEqual({ kind: "draw", enabled: true });
	});

	it("gives a non-victim responder no action inside a held window", () => {
		snapshot({}, []);
		openHeldWindow(["alice", "carol"]);

		expect(storeGame.windowActionState(NOW + 5000)).toBeNull();
	});

	it("gives a victim who is not a responder no action", () => {
		snapshot({}, [DEBT_STATUS]);
		openHeldWindow(["carol"]);

		expect(storeGame.windowActionState(NOW + 5000)).toBeNull();
	});

	it("keeps the legacy pass for any responder of a window without hold", () => {
		snapshot({}, []);
		handler("match_event")(
			frame(1, "window_open", {
				window_id: "2",
				kinds: ["generic"],
				duration_ms: 5000,
				deadline_ms: 5000,
				responders: ["alice"],
				eligible_filter_digest: "d"
			})
		);

		expect(storeGame.windowActionState(NOW)).toEqual({ kind: "pass", enabled: true });
	});

	it("gives spectators no action", () => {
		snapshot({}, [DEBT_STATUS]);
		storeAuth.username = "zed";
		openHeldWindow(["alice"]);

		expect(storeGame.windowActionState(NOW + 5000)).toBeNull();
	});
});
