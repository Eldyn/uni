import { describe, it, expect, beforeEach, vi } from "vitest";

// Mock ws.svelte so the store's listeners can be driven by hand and emits
// captured, matching the other game-store tests.
vi.mock("$lib/stores/ws.svelte", () => ({
	ServerAction: {
		Error: "error",
		MatchStateUpdated: "match_state_updated",
		MatchOver: "match_over",
		MatchEvent: "match_event"
	},
	ClientAction: {
		MatchPlayCard: "match_play_card",
		MatchDrawCard: "match_draw_card",
		MatchKeepDrawn: "match_keep_drawn"
	},
	ws: {
		on(action: string, handler: (data: unknown) => void) {
			if (!(globalThis as any).__wsTestHandlers) (globalThis as any).__wsTestHandlers = {};
			(globalThis as any).__wsTestHandlers[action] = handler;
			return () => {};
		},
		onOpen() {
			return () => {};
		},
		onClose() {
			return () => {};
		},
		emit: vi.fn(),
		emitAndWait: async () => ({ ok: false })
	}
}));

import { storeGame, type GameState } from "$lib/stores/game.svelte";
import { CardBus } from "$components/game/card-bus.svelte";
import { storeAuth } from "$lib/stores/auth.svelte";
import { ws } from "$lib/stores/ws.svelte";

function snapshotHandler(): (payload: any) => void {
	const handler = (globalThis as any).__wsTestHandlers?.["match_state_updated"];
	if (!handler) throw new Error("MatchStateUpdated handler was not registered");
	return handler;
}

/** Minimal state with just the fields the pending tests read. */
function stateWithPending(pending: GameState["pendingPlayDrawn"]): GameState {
	return {
		active_type: "white",
		current_turn: "alice",
		play_direction: 1,
		players: [
			{ username: "alice", card_count: 0, is_bot: false },
			{ username: "bob", card_count: 0, is_bot: false }
		],
		pending_draws: 0,
		draw_pile_size: 0,
		pendingPlayDrawn: pending
	} as unknown as GameState;
}

describe("storeGame maps pending_play_drawn", () => {
	beforeEach(() => {
		storeGame.state = null;
	});

	it("accepts an explicit null", () => {
		snapshotHandler()({
			match_state: { current_player: "alice", players: [], pending_play_drawn: null }
		});
		expect(storeGame.state?.pendingPlayDrawn).toBeNull();
	});

	it("accepts the owner shape (with a card id)", () => {
		snapshotHandler()({
			match_state: {
				current_player: "alice",
				players: [],
				pending_play_drawn: { player: "alice", card: 4242 }
			}
		});
		expect(storeGame.state?.pendingPlayDrawn).toEqual({ player: "alice", card: 4242 });
	});

	it("accepts the opponent shape (no card id)", () => {
		snapshotHandler()({
			match_state: {
				current_player: "bob",
				players: [],
				pending_play_drawn: { player: "bob" }
			}
		});
		const pending = storeGame.state?.pendingPlayDrawn;
		expect(pending).toEqual({ player: "bob" });
		expect(pending?.card).toBeUndefined();
	});

	it("defaults to null when the field is absent (older payloads)", () => {
		snapshotHandler()({ match_state: { current_player: "alice", players: [] } });
		expect(storeGame.state?.pendingPlayDrawn).toBeNull();
	});
});

describe("storeGame.applyPendingPlayDrawn drives the card bus", () => {
	beforeEach(() => {
		storeGame.state = null;
	});

	it("lifts the owner's drawn card by id and clears it when the field goes null", () => {
		const bus = new CardBus();

		storeGame.state = stateWithPending({ player: "alice", card: 77 });
		storeGame.applyPendingPlayDrawn(bus, "alice");
		expect(bus.pendingLocalPlayDrawnId).toBe(77);
		expect(bus.isHoldingOpponent("alice")).toBe(false);

		storeGame.state = stateWithPending(null);
		storeGame.applyPendingPlayDrawn(bus, "alice");
		expect(bus.pendingLocalPlayDrawnId).toBeNull();
	});

	it("lifts an opponent's card back without ever exposing the id", () => {
		const bus = new CardBus();
		storeGame.state = stateWithPending({ player: "bob" });
		storeGame.applyPendingPlayDrawn(bus, "alice");
		expect(bus.pendingLocalPlayDrawnId).toBeNull();
		expect(bus.isHoldingOpponent("bob")).toBe(true);
	});

	it("clears a stale opponent hold when the field resolves", () => {
		const bus = new CardBus();
		storeGame.state = stateWithPending({ player: "bob" });
		storeGame.applyPendingPlayDrawn(bus, "alice");
		expect(bus.isHoldingOpponent("bob")).toBe(true);

		storeGame.state = stateWithPending(null);
		storeGame.applyPendingPlayDrawn(bus, "alice");
		expect(bus.isHoldingOpponent("bob")).toBe(false);
		expect(bus.holdingOpponents.size).toBe(0);
	});
});

describe("storeGame.keepDrawn", () => {
	beforeEach(() => {
		storeGame.state = stateWithPending(null);
		storeGame.isActionPending = false;
		storeAuth.username = "alice";
		vi.clearAllMocks();
	});

	it("sends match_keep_drawn for the owner's live decision", () => {
		storeGame.state = stateWithPending({ player: "alice", card: 77 });
		storeGame.keepDrawn();
		expect(ws.emit).toHaveBeenCalledWith("match_keep_drawn");
	});

	it("is a no-op while another action is already pending", () => {
		storeGame.state = stateWithPending({ player: "alice", card: 77 });
		storeGame.isActionPending = true;
		storeGame.keepDrawn();
		expect(ws.emit).not.toHaveBeenCalled();
	});

	it("is a no-op when no decision is live", () => {
		storeGame.keepDrawn();
		expect(ws.emit).not.toHaveBeenCalled();
	});

	it("is a no-op when another player owns the decision", () => {
		storeGame.state = stateWithPending({ player: "bob" });
		storeGame.keepDrawn();
		expect(ws.emit).not.toHaveBeenCalled();
	});
});
