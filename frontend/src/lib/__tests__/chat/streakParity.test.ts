import { describe, expect, it } from "vitest";
import { StreakTracker } from "../../chat/playLog/streakTracker";
import type { LogEvent } from "../../chat/playLog/logEvent";

type GoldenRow = {
	event: string;
	log_event: { kind: string; player?: string; card_kind?: string } | null;
	old: { skip_run: number; reverse_run: number };
	server: { skip_run: number; reverse_run: number };
};

const SEEDS = [7, 11, 23];

const GOLDEN_FILES = import.meta.glob<GoldenRow[]>(
	"../../../../../tests/fixtures/golden_streak_parity/seed_*.json",
	{ eager: true, import: "default" }
);

function loadGolden(seed: number): GoldenRow[] {
	const key = Object.keys(GOLDEN_FILES).find((path) => path.endsWith(`seed_${seed}.json`));
	if (key === undefined) throw new Error(`missing golden file for seed ${seed}`);
	return GOLDEN_FILES[key];
}

function toLogEvent(row: NonNullable<GoldenRow["log_event"]>, seq: number): LogEvent {
	const player = row.player ?? "";
	switch (row.kind) {
		case "skip":
			return { kind: "skip", seq, player };
		case "reverse":
			return { kind: "reverse", seq };
		case "draw":
			return { kind: "draw", seq, player, count: 1, penalty: false };
		case "keep":
			return { kind: "keep", seq, player };
		default:
			return {
				kind: row.kind as "play" | "auto_play",
				seq,
				player,
				cardKind: `uni:${row.card_kind ?? ""}`,
				color: "red"
			};
	}
}

describe("server streak parity with the real StreakTracker", () => {
	for (const seed of SEEDS) {
		it(`seed ${seed}: the old values of the golden file are the tracker values`, () => {
			const tracker = new StreakTracker();
			let checked = 0;
			for (const [index, row] of loadGolden(seed).entries()) {
				if (row.log_event === null) continue;
				const info = tracker.record(toLogEvent(row.log_event, index));
				expect(info.skipRun, `step ${index}`).toBe(row.old.skip_run);
				expect(info.reverseRun, `step ${index}`).toBe(row.old.reverse_run);
				expect(row.server.skip_run, `step ${index}`).toBe(row.old.skip_run);
				expect(row.server.reverse_run, `step ${index}`).toBe(row.old.reverse_run);
				checked += 1;
			}
			expect(checked).toBeGreaterThan(0);
		});
	}
});
