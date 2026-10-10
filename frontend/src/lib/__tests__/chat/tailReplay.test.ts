import { describe, expect, it } from "vitest";
import { replayTailToLog, type TailEnvelope } from "../../chat/playLog/tailReplay";
import type { LogLine } from "../../chat/playLog/logEvent";

const RED_FIVE_BITS = 1000;
const WILD_BITS = 2000;

function resolveKind(bits: number): { kind: string; color: string } | undefined {
	if (bits === RED_FIVE_BITS) return { kind: "uni:red_5", color: "red" };
	if (bits === WILD_BITS) return { kind: "uni:wild", color: "white" };
	return undefined;
}

function envelope(
	sequence: number,
	id: string,
	payload: Record<string, unknown>,
	actor?: string,
	context?: Record<string, unknown>
): TailEnvelope {
	return { sequence, seq: sequence, id, type: id, actor, replayed: true, payload, context };
}

function replay(tail: TailEnvelope[]): LogLine[] {
	const lines: LogLine[] = [];
	replayTailToLog(tail, resolveKind, (line) => lines.push(line));
	return lines;
}

describe("replayTailToLog", () => {
	it("turns a retained play into a play line", () => {
		const lines = replay([
			envelope(
				0,
				"card_played",
				{ player: "Ann", card: RED_FIVE_BITS, from_zone_ordinal: 0 },
				"Ann"
			)
		]);
		expect(lines.map((line) => line.key)).toEqual(["uni:log.play"]);
		expect(lines[0].params).toMatchObject({ name: "Ann", kind: "uni:red_5", color: "red" });
	});

	it("waits for the color of a wild and logs the play and the wild line", () => {
		const lines = replay([
			envelope(0, "card_played", { player: "Ann", card: WILD_BITS, from_zone_ordinal: 0 }, "Ann"),
			envelope(1, "active_type_changed", { color: "blue", previous_color: "red" }, "Ann")
		]);
		expect(lines[0].params).toMatchObject({ color: "blue" });
		expect(lines.length).toBeGreaterThan(1);
	});

	it("logs a skip, a reverse, a keep and a win", () => {
		const lines = replay([
			envelope(0, "turn_skipped", { target: "Bob" }, "Ann"),
			envelope(1, "direction_changed", { direction: -1 }, "Ann"),
			envelope(2, "drawn_card_kept", { target: "Bob" }, "Bob"),
			envelope(3, "match_ended", { target: "Ann", targets: ["Ann", "Bob"] })
		]);
		expect(lines.map((line) => line.key)).toEqual([
			"uni:log.skip",
			"uni:log.reverse",
			expect.stringMatching(/^uni:log\.keep/),
			expect.stringMatching(/^uni:log\.win/)
		]);
	});

	it("ignores an id that has no log line and an unknown card", () => {
		const lines = replay([
			envelope(0, "turn_advance", { from: "Ann", to: "Bob" }),
			envelope(1, "some_mod:thing", {}),
			envelope(2, "card_played", { player: "Ann", card: 7, from_zone_ordinal: 0 })
		]);
		expect(lines).toEqual([]);
	});

	it("is deterministic: the same tail gives the same lines", () => {
		const tail = [
			envelope(0, "turn_skipped", { target: "Bob" }, "Ann"),
			envelope(1, "turn_skipped", { target: "Bob" }, "Ann")
		];
		expect(replay(tail)).toEqual(replay(tail));
	});

	it("marks a debt draw as a penalty and a plain draw as none", () => {
		const debt = replay([
			envelope(0, "cards_drawn", { target: "Bob", count: 4, from_debt: true }, "Bob")
		]);
		const plain = replay([envelope(0, "cards_drawn", { target: "Bob", count: 1 }, "Bob")]);
		expect(debt.map((line) => line.key)).toEqual(["uni:log.draw_stack"]);
		expect(plain[0].key).not.toMatch(/stack/);
	});

	it("logs the near-win line from the near_win_entered event, after the play", () => {
		const lines = replay([
			envelope(
				0,
				"card_played",
				{ player: "Ann", card: RED_FIVE_BITS, from_zone_ordinal: 0 },
				"Ann"
			),
			envelope(1, "near_win_entered", { target: "Ann" }, "Ann")
		]);
		expect(lines.map((line) => line.key)).toEqual([
			"uni:log.play",
			expect.stringMatching(/^uni:log\.near_win/)
		]);
		expect(lines[1].params).toEqual({ name: "Ann" });
	});

	it("keeps the near-win line of a wild between its play line and its wild line", () => {
		const lines = replay([
			envelope(0, "card_played", { player: "Ann", card: WILD_BITS, from_zone_ordinal: 0 }, "Ann"),
			envelope(1, "near_win_entered", { target: "Ann" }, "Ann"),
			envelope(2, "active_type_changed", { color: "blue", previous_color: "red" }, "Ann", {
				"uni:wild_run": 1
			})
		]);
		expect(lines.map((line) => line.key)).toEqual([
			"uni:log.play",
			expect.stringMatching(/^uni:log\.near_win/),
			expect.stringMatching(/^uni:log\.wild_reaction/)
		]);
	});

	it("logs the blocked line from the blocked event with the envelope actor", () => {
		const lines = replay([
			envelope(0, "turn_skipped", { target: "Bob" }, "Ann", { "uni:skip_run": 1 }),
			envelope(1, "blocked", { target: "Bob" }, "Ann")
		]);
		expect(lines.map((line) => line.key)).toEqual([
			"uni:log.skip",
			expect.stringMatching(/^uni:log\.blocked_/)
		]);
		expect(lines[1].params).toEqual({ blocker: "Ann", target: "Bob" });
	});

	it("logs no blocked line when the envelope has no actor", () => {
		const lines = replay([envelope(0, "blocked", { target: "Bob" })]);
		expect(lines).toEqual([]);
	});

	it("reads the draw counts and the wild run from the envelope context", () => {
		const lines = replay([
			envelope(
				0,
				"cards_drawn",
				{ target: "Ann", player: "Ann", count: 1, hand_size_after: 3, held: false },
				"Ann",
				{ "uni:draw_run": { Ann: 4, Bob: 1 }, "uni:total_draws": 5 }
			),
			envelope(1, "card_played", { player: "Bob", card: WILD_BITS, from_zone_ordinal: 0 }, "Bob"),
			envelope(2, "active_type_changed", { color: "green", previous_color: "red" }, "Bob", {
				"uni:wild_run": 3,
				"uni:last_wild_color": { last: "green", previous: "blue" }
			})
		]);
		expect(lines[0].key).toMatch(/^uni:log\.draw_many/);
		const wild = lines.find((line) => line.key.startsWith("uni:log.wild_reaction"));
		expect(wild?.key).toMatch(/^uni:log\.wild_reaction_third/);
		expect(wild?.params.prevColor).toBe("blue");
	});

	it("counts nothing itself: a tail with no context reads as no streak info", () => {
		const lines = replay([
			envelope(0, "turn_skipped", { target: "Bob" }, "Ann"),
			envelope(1, "turn_skipped", { target: "Bob" }, "Ann"),
			envelope(2, "turn_skipped", { target: "Bob" }, "Ann")
		]);
		expect(lines.map((line) => line.key)).toEqual(["uni:log.skip", "uni:log.skip", "uni:log.skip"]);
	});
});
