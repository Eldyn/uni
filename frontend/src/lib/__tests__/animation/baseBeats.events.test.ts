import { describe, it, expect } from "vitest";
import { mapMatchEventPacket } from "$components/game/animation/baseBeats.svelte";

/** One server frame as the ws layer delivers it: envelope fields plus the
 *  transport's own `action` discriminator. */
function frame(seq: number, type: string, payload: Record<string, unknown>) {
	return { action: "match_event", seq, type, payload };
}

describe("mapMatchEventPacket", () => {
	it("maps card_played to a play beat", () => {
		const beat = mapMatchEventPacket(
			frame(1, "card_played", { player: "alice", card: 42, from_zone_ordinal: 3 })
		);
		expect(beat).toEqual({
			seq: 1,
			kind: "play",
			player: "alice",
			cardId: 42,
			fromZoneOrdinal: 3,
			auto: false
		});
	});

	it("maps auto_played to the play-beat variant with a trigger summary", () => {
		const beat = mapMatchEventPacket(
			frame(2, "auto_played", { player: "bot-1", card: 7, trigger_summary: "forced draw" })
		);
		expect(beat).toEqual({
			seq: 2,
			kind: "play",
			player: "bot-1",
			cardId: 7,
			auto: true,
			triggerSummary: "forced draw"
		});
	});

	it("maps cards_drawn to a draw beat carrying owner card identities when present", () => {
		const beat = mapMatchEventPacket(
			frame(3, "cards_drawn", { player: "alice", count: 2, source_pile: "draw", cards: [10, 11] })
		);
		expect(beat).toEqual({
			seq: 3,
			kind: "draw",
			player: "alice",
			count: 2,
			sourcePile: "draw",
			cardIds: [10, 11]
		});
	});

	it("maps cards_drawn with no identities (non-owner viewers) to an empty cardIds list", () => {
		const beat = mapMatchEventPacket(
			frame(4, "cards_drawn", { player: "bob", count: 3, source_pile: "draw" })
		);
		expect(beat).toEqual({
			seq: 4,
			kind: "draw",
			player: "bob",
			count: 3,
			sourcePile: "draw",
			cardIds: []
		});
	});

	it("maps reshuffle to a reshuffle beat", () => {
		const beat = mapMatchEventPacket(frame(5, "reshuffle", { draw_size: 20, discard_size: 1 }));
		expect(beat).toEqual({ seq: 5, kind: "reshuffle", drawSize: 20, discardSize: 1 });
	});

	it("maps turn_advance to a turn tween beat", () => {
		const beat = mapMatchEventPacket(
			frame(6, "turn_advance", { from: "alice", to: "bob", direction: 1, deadline_ms: 15000 })
		);
		expect(beat).toEqual({
			seq: 6,
			kind: "turn",
			from: "alice",
			to: "bob",
			direction: 1,
			deadlineMs: 15000
		});
	});

	it("maps status_applied to a toast beat", () => {
		const beat = mapMatchEventPacket(
			frame(7, "status_applied", {
				target: "bob",
				status_kind: "frozen",
				magnitude: 2,
				duration_unit: "turns",
				instance_id: 9
			})
		);
		expect(beat).toEqual({
			seq: 7,
			kind: "toast",
			target: "bob",
			statusKind: "frozen",
			magnitude: 2,
			durationUnit: "turns",
			instanceId: 9
		});
	});

	it("ignores unknown packet types (forward-compat)", () => {
		expect(mapMatchEventPacket(frame(8, "some_future_event", { anything: true }))).toBeNull();
	});

	it("ignores malformed envelopes and known types with invalid payloads", () => {
		expect(mapMatchEventPacket({ type: "card_played", payload: {} })).toBeNull();
		expect(mapMatchEventPacket(frame(9, "card_played", { player: "alice" }))).toBeNull();
		expect(mapMatchEventPacket(null)).toBeNull();
	});
});
