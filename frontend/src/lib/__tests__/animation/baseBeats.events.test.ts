import { describe, it, expect } from "vitest";
import { mapMatchEventPacket } from "$components/game/animation/baseBeats.svelte";

/** One server frame as the ws layer delivers it: envelope fields plus the
 *  transport's own `action` discriminator. */
function frame(seq: number, type: string, payload: Record<string, unknown>) {
	return { action: "match_event", sequence: seq, id: type, payload };
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
			frame(3, "cards_drawn", {
				target: "alice",
				count: 2,
				hand_size_after: 5,
				from_debt: false,
				source_pile: "draw",
				cards: [10, 11]
			})
		);
		expect(beat).toEqual({
			seq: 3,
			kind: "draw",
			player: "alice",
			count: 2,
			sourcePile: "draw",
			cardIds: [10, 11],
			handSizeAfter: 5,
			fromDebt: false
		});
	});

	it("maps cards_drawn with no identities (non-owner viewers) to an empty cardIds list", () => {
		const beat = mapMatchEventPacket(
			frame(4, "cards_drawn", {
				target: "bob",
				count: 3,
				hand_size_after: 5,
				from_debt: false,
				source_pile: "draw"
			})
		);
		expect(beat).toEqual({
			seq: 4,
			kind: "draw",
			player: "bob",
			count: 3,
			sourcePile: "draw",
			cardIds: [],
			handSizeAfter: 5,
			fromDebt: false
		});
	});

	it("maps the contract fields of cards_drawn onto the draw beat", () => {
		const beat = mapMatchEventPacket(
			frame(30, "cards_drawn", {
				target: "alice",
				count: 2,
				hand_size_after: 9,
				from_debt: true,
				source_pile: "draw"
			})
		);
		expect(beat).toEqual({
			seq: 30,
			kind: "draw",
			player: "alice",
			count: 2,
			sourcePile: "draw",
			cardIds: [],
			handSizeAfter: 9,
			fromDebt: true
		});
	});

	it("maps drawn_card_kept to a keep beat with the owner-only card", () => {
		expect(
			mapMatchEventPacket(frame(31, "drawn_card_kept", { target: "alice", card: 77 }))
		).toEqual({ seq: 31, kind: "keep", player: "alice", cardId: 77 });
		expect(mapMatchEventPacket(frame(32, "drawn_card_kept", { target: "bob" }))).toEqual({
			seq: 32,
			kind: "keep",
			player: "bob"
		});
	});

	it("drops a drawn_card_kept with no target", () => {
		expect(mapMatchEventPacket(frame(33, "drawn_card_kept", {}))).toBeNull();
	});

	it("maps hands_swapped to a hands_swap beat", () => {
		const beat = mapMatchEventPacket(
			frame(6, "hands_swapped", {
				source: "alice",
				target: "bob",
				count: 3,
				target_count: 1
			})
		);
		expect(beat).toEqual({
			seq: 6,
			kind: "hands_swap",
			a: "alice",
			b: "bob",
			aSize: 3,
			bSize: 1
		});
	});

	it("maps hands_passed to a hands_pass beat", () => {
		const beat = mapMatchEventPacket(
			frame(7, "hands_passed", {
				direction: "backward",
				targets: ["alice", "bob", "cara"],
				counts: [3, 1, 2]
			})
		);
		expect(beat).toEqual({
			seq: 7,
			kind: "hands_pass",
			direction: "backward",
			players: ["alice", "bob", "cara"],
			handSizes: [3, 1, 2]
		});
	});

	it("drops malformed hand movement payloads", () => {
		expect(mapMatchEventPacket(frame(8, "hands_swapped", { source: "alice" }))).toBeNull();
		expect(mapMatchEventPacket(frame(9, "hands_passed", { direction: "forward" }))).toBeNull();
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
			deadlineMs: 15000,
			skipped: []
		});
	});

	it("maps direction_changed to a direction beat", () => {
		const beat = mapMatchEventPacket(frame(8, "direction_changed", { direction: -1 }));
		expect(beat).toEqual({ seq: 8, kind: "direction", direction: -1 });
	});

	it("drops a direction_changed frame with an invalid payload", () => {
		expect(mapMatchEventPacket(frame(8, "direction_changed", {}))).toBeNull();
	});

	it("maps turn_skipped to a skip beat with the actor", () => {
		const beat = mapMatchEventPacket({
			...frame(9, "turn_skipped", { target: "bob" }),
			actor: "alice"
		});
		expect(beat).toEqual({ seq: 9, kind: "skip", target: "bob", actor: "alice" });
	});

	it("maps placement_set to a placement beat", () => {
		const beat = mapMatchEventPacket(
			frame(10, "placement_set", { target: "bob", place: 2, is_elimination: false })
		);
		expect(beat).toEqual({
			seq: 10,
			kind: "placement_set",
			target: "bob",
			place: 2,
			isElimination: false
		});
	});

	it("maps match_ended to a match end beat", () => {
		const beat = mapMatchEventPacket(
			frame(11, "match_ended", { target: "ann", targets: ["ann", "bob"], final_digest: "x" })
		);
		expect(beat).toEqual({ seq: 11, kind: "match_ended", target: "ann", targets: ["ann", "bob"] });
	});

	it("maps turn_skipped without an actor", () => {
		const beat = mapMatchEventPacket(frame(9, "turn_skipped", { target: "bob" }));
		expect(beat).toEqual({ seq: 9, kind: "skip", target: "bob", actor: undefined });
	});

	it("drops a turn_skipped frame with an invalid payload", () => {
		expect(mapMatchEventPacket(frame(9, "turn_skipped", {}))).toBeNull();
	});

	it("maps active_type_changed to an active color beat", () => {
		const beat = mapMatchEventPacket(
			frame(10, "active_type_changed", { color: "blue", previous_color: "red" })
		);
		expect(beat).toEqual({
			seq: 10,
			kind: "active_color",
			color: "blue",
			previousColor: "red"
		});
	});

	it("drops an active_type_changed frame with an invalid payload", () => {
		expect(mapMatchEventPacket(frame(10, "active_type_changed", { color: "blue" }))).toBeNull();
	});

	it.each([
		["extra_turn_granted", { target: "bob" }],
		["restriction_added", { restriction_id: "mod:rule", phase: "deny" }],
		["restriction_removed", { restriction_id: "mod:rule" }],
		["turn_timer_changed", { target: "bob", duration_ms: 5000 }],
		["hands_redistributed", { targets: ["alice", "bob"], counts: [2, 3] }],
		["card_moved", { card: 4097, from_zone: "hand", to_zone: "hand", target: "bob" }],
		["card_moved", { from_zone: "hand", to_zone: "limbo", source: "alice" }],
		["card_created", { zone: "hand", target: "bob" }],
		["card_removed", { from_zone: "hand", target: "bob" }],
		["card_replaced", { zone: "hand", target: "bob" }]
	])("maps %s to a beat that the client ignores", (type, payload) => {
		expect(mapMatchEventPacket(frame(11, type, payload))).toEqual({
			seq: 11,
			kind: "ignored",
			eventId: type
		});
	});

	it("maps a mod event to an ignored beat that names the mod event id", () => {
		expect(
			mapMatchEventPacket({
				...frame(12, "mod_event", { target: "alice", count: 3 }),
				id: "my_mod:apple_eaten"
			})
		).toEqual({ seq: 12, kind: "ignored", eventId: "my_mod:apple_eaten" });
	});

	it("maps a mod event with an empty payload to an ignored beat", () => {
		expect(
			mapMatchEventPacket({ ...frame(13, "mod_event", {}), id: "my_mod:secret_only" })
		).toEqual({ seq: 13, kind: "ignored", eventId: "my_mod:secret_only" });
	});

	it("ignores a mod event that has no id", () => {
		expect(mapMatchEventPacket(frame(14, "mod_event", {}))).toBeNull();
	});

	it.each([
		["extra_turn_granted", {}],
		["restriction_added", { restriction_id: "mod:rule" }],
		["restriction_removed", {}],
		["turn_timer_changed", { target: "bob" }],
		["hands_redistributed", { targets: ["alice"] }],
		["card_moved", { from_zone: "hand" }],
		["card_created", {}],
		["card_removed", { target: "bob" }],
		["card_replaced", {}]
	])("drops a %s frame with an invalid payload", (type, payload) => {
		expect(mapMatchEventPacket(frame(11, type, payload))).toBeNull();
	});

	it("carries the skipped seats named on turn_advance", () => {
		const beat = mapMatchEventPacket(
			frame(10, "turn_advance", {
				from: "alice",
				to: "carol",
				direction: 1,
				deadline_ms: 15000,
				skipped: ["bob"]
			})
		);
		expect(beat).toEqual({
			seq: 10,
			kind: "turn",
			from: "alice",
			to: "carol",
			direction: 1,
			deadlineMs: 15000,
			skipped: ["bob"]
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

	it("maps near_win_entered to a near-win beat", () => {
		expect(mapMatchEventPacket(frame(5, "near_win_entered", { target: "alice" }))).toEqual({
			seq: 5,
			kind: "near_win",
			target: "alice"
		});
	});

	it("maps blocked to a blocked beat with the envelope actor", () => {
		expect(
			mapMatchEventPacket({ ...frame(6, "blocked", { target: "alice" }), actor: "bob" })
		).toEqual({ seq: 6, kind: "blocked", target: "alice", actor: "bob" });
	});

	it("drops a derived event whose payload is malformed", () => {
		expect(mapMatchEventPacket(frame(7, "near_win_entered", {}))).toBeNull();
		expect(mapMatchEventPacket(frame(8, "blocked", { target: 4 }))).toBeNull();
	});

	it("carries the envelope context on a draw and a color change", () => {
		const context = { "uni:total_draws": 3 };
		const draw = mapMatchEventPacket({
			...frame(9, "cards_drawn", {
				target: "alice",
				count: 1,
				hand_size_after: 5,
				from_debt: false,
				source_pile: "draw"
			}),
			context
		});
		expect(draw).toMatchObject({ kind: "draw", context });
		const color = mapMatchEventPacket({
			...frame(10, "active_type_changed", { color: "blue", previous_color: "red" }),
			context: { "uni:wild_run": 2 }
		});
		expect(color).toMatchObject({ kind: "active_color", context: { "uni:wild_run": 2 } });
	});
});
