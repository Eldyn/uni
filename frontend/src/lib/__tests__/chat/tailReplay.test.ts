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
	actor?: string
): TailEnvelope {
	return { sequence, seq: sequence, id, type: id, actor, replayed: true, payload };
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
		expect(lines.map((line) => line.key)).toEqual(["log_play"]);
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
			"log_skip",
			"log_reverse",
			expect.stringMatching(/^log_keep/),
			expect.stringMatching(/^log_win/)
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
		expect(debt.map((line) => line.key)).toEqual(["log_draw_stack"]);
		expect(plain[0].key).not.toMatch(/stack/);
	});
});
