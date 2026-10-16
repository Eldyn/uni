import { describe, it, expect, beforeEach, vi } from "vitest";

const emitSpy = vi.hoisted(() => vi.fn());

vi.mock("$lib/stores/ws.svelte", () => ({
	ServerAction: {
		Success: "success",
		Error: "error",
		MatchStateUpdated: "match_state_updated",
		MatchEvent: "match_event",
		MatchOver: "match_over"
	},
	ClientAction: {
		MatchPlayCard: "match_play_card",
		MatchDrawCard: "match_draw_card",
		MatchSnapshotRequest: "match_snapshot_request"
	},
	ws: {
		on(action: string, handler: unknown) {
			const handlers = ((globalThis as Record<string, unknown>).__wsTestHandlers ??= {}) as Record<
				string,
				unknown
			>;
			handlers[action] = handler;
			return () => {};
		},
		onOpen: () => () => {},
		onClose: () => () => {},
		emit: emitSpy,
		emitAndWait: async () => ({ ok: false })
	}
}));

import { storeGame } from "$lib/stores/game.svelte";

function handler(action: string): (payload: unknown) => void {
	const registered = (globalThis as Record<string, any>).__wsTestHandlers?.[action];
	if (!registered) throw new Error(`${action} handler was not registered`);
	return registered;
}

function envelope(sequence: number, id = "round_advance", extra: Record<string, unknown> = {}) {
	return { action: "match_event", sequence, id, payload: {}, ...extra };
}

function snapshot(seqWatermark: number) {
	return {
		match_state: {
			current_player: "alice",
			active_type: "red",
			players: [{ username: "alice", card_count: 5, is_bot: false }],
			draw_pile: { count: 40 },
			seq_watermark: seqWatermark
		},
		action_required: null
	};
}

function snapshotRequests() {
	return emitSpy.mock.calls.filter(([action]) => action === "match_snapshot_request");
}

describe("storeGame snapshot request after a sequence gap", () => {
	beforeEach(() => {
		storeGame.reset();
		emitSpy.mockClear();
	});

	it("sends no request while the sequence has no gap", () => {
		const onEvent = handler("match_event");
		onEvent(envelope(0));
		onEvent(envelope(1));
		onEvent(envelope(2));

		expect(snapshotRequests()).toHaveLength(0);
		expect(storeGame.desynced).toBe(false);
	});

	it("sends no request when only another seat missed an event", () => {
		// This seat never sees the one-seat event, so its own numbers stay
		// consecutive even if another seat has a larger count.
		const onEvent = handler("match_event");
		onEvent(envelope(0, "card_played"));
		onEvent(envelope(1, "turn_advance"));

		expect(snapshotRequests()).toHaveLength(0);
	});

	it("sends one request on a real gap and recovers on the snapshot", () => {
		const onEvent = handler("match_event");
		onEvent(envelope(0));
		onEvent(envelope(2));

		expect(storeGame.desynced).toBe(true);
		expect(snapshotRequests()).toHaveLength(1);

		handler("match_state_updated")(snapshot(5));
		expect(storeGame.desynced).toBe(false);

		onEvent(envelope(5));
		expect(storeGame.desynced).toBe(false);
		expect(snapshotRequests()).toHaveLength(1);
	});

	it("sends one request while a snapshot is pending", () => {
		const onEvent = handler("match_event");
		onEvent(envelope(0));
		onEvent(envelope(2));
		onEvent(envelope(5));
		onEvent(envelope(9));

		expect(snapshotRequests()).toHaveLength(1);
	});

	it("asks again for a new gap after the snapshot answered", () => {
		const onEvent = handler("match_event");
		onEvent(envelope(0));
		onEvent(envelope(2));
		handler("match_state_updated")(snapshot(3));
		onEvent(envelope(3));
		onEvent(envelope(6));

		expect(snapshotRequests()).toHaveLength(2);
	});
});

describe("storeGame snapshot request retry", () => {
	beforeEach(() => {
		storeGame.reset();
		emitSpy.mockClear();
		vi.useFakeTimers();
	});

	it("asks again after the retry delay when the server stayed silent", () => {
		const onEvent = handler("match_event");
		onEvent(envelope(0));
		onEvent(envelope(2));
		expect(snapshotRequests()).toHaveLength(1);

		vi.advanceTimersByTime(2000);
		onEvent(envelope(3));

		expect(snapshotRequests()).toHaveLength(2);
		vi.useRealTimers();
	});
});
