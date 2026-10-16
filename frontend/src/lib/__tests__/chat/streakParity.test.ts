import { describe, expect, it } from "vitest";

/**
 * The old client counters were computed by a tracker class. The class is
 * deleted. Its values are stored in `expected_<name>.json`: the real tracker
 * ran over the log events of each golden match once, before the deletion.
 * Nothing writes the expected files now. This test reads both stored files
 * and checks the server values (in `<name>.json`, written by the C++ test
 * `streak_parity_test.cpp`) against the stored tracker values, so parity
 * stays pinned.
 */

type GoldenLogEvent = { kind: string; player?: string };

type GoldenWindow = { target: string | null; open: boolean };

type GoldenRow = {
	event: string;
	log_events: GoldenLogEvent[];
	server: {
		skip_run?: number;
		reverse_run?: number;
		draw_run?: Record<string, number>;
		total_draws?: number;
		wild_run?: number;
		last_wild_color?: { last: string | null; previous: string | null };
		near_win?: GoldenWindow;
		near_win_before?: GoldenWindow;
		blocked?: boolean;
	};
};

type ExpectedRow = {
	event: string;
	old: {
		skip_run: number;
		reverse_run: number;
		draw_run: number;
		total_draws: number;
		wild_run: number;
		last_wild_color: string | null;
		prev_wild_color: string | null;
		near_win_target: string | null;
		near_win_open: boolean;
	} | null;
};

const SEEDS = [7, 11, 23, 1, 13, 17, 19, 27, 30, 42];
const SCENARIOS = ["skip_block", "penalty_block", "escape", "replace", "skip_repeat"];
const NAMES = [
	...SEEDS.map((seed) => `seed_${seed}`),
	...SCENARIOS.map((name) => `scenario_${name}`)
];

const GOLDEN_FILES = import.meta.glob<GoldenRow[]>(
	"../../../../../tests/fixtures/golden_streak_parity/{seed,scenario}_*.json",
	{ eager: true, import: "default" }
);
const EXPECTED_FILES = import.meta.glob<ExpectedRow[]>(
	"../../../../../tests/fixtures/golden_streak_parity/expected_*.json",
	{ eager: true, import: "default" }
);

function load<T>(files: Record<string, T>, file: string): T {
	const key = Object.keys(files).find((path) => path.endsWith(`/${file}`));
	if (key === undefined) throw new Error(`missing stored file ${file}`);
	return files[key];
}

function drawerOf(row: GoldenRow): string | undefined {
	return row.log_events.find((logEvent) => logEvent.kind === "draw")?.player;
}

describe("server streak values equal the stored values of the old tracker", () => {
	for (const name of NAMES) {
		it(`${name}: every counter at every step with a log event`, () => {
			const golden = load(GOLDEN_FILES, `${name}.json`);
			const expected = load(EXPECTED_FILES, `expected_${name}.json`);
			expect(golden).toHaveLength(expected.length);
			let compared = 0;
			golden.forEach((row, index) => {
				const old = expected[index].old;
				if (row.log_events.length === 0 || old === null) return;
				const where = `${name} step ${index} ${row.event}`;
				expect(row.server.skip_run, where).toBe(old.skip_run);
				expect(row.server.reverse_run, where).toBe(old.reverse_run);
				expect(row.server.total_draws, where).toBe(old.total_draws);
				expect(row.server.wild_run, where).toBe(old.wild_run);
				expect(row.server.last_wild_color?.last, where).toBe(old.last_wild_color);
				expect(row.server.last_wild_color?.previous, where).toBe(old.prev_wild_color);
				const drawer = drawerOf(row);
				if (drawer !== undefined) {
					expect(row.server.draw_run?.[drawer], where).toBe(old.draw_run);
				}
				const window = row.server.blocked ? row.server.near_win_before : row.server.near_win;
				expect(window?.open, where).toBe(old.near_win_open);
				if (window?.open) expect(window.target, where).toBe(old.near_win_target);
				compared += 1;
			});
			expect(compared).toBeGreaterThan(0);
		});
	}

	it("the stored matches reach every counter", () => {
		const reached = {
			skip: 0,
			reverse: 0,
			draw: 0,
			wild: 0,
			wildColor: 0,
			nearWin: 0,
			blocked: 0
		};
		for (const name of NAMES) {
			for (const [index, row] of load(GOLDEN_FILES, `${name}.json`).entries()) {
				const old = load(EXPECTED_FILES, `expected_${name}.json`)[index].old;
				if (old === null) continue;
				reached.skip += old.skip_run >= 2 ? 1 : 0;
				reached.reverse += old.reverse_run >= 2 ? 1 : 0;
				reached.draw += old.draw_run >= 4 ? 1 : 0;
				reached.wild += old.wild_run >= 2 ? 1 : 0;
				reached.wildColor += old.prev_wild_color !== null ? 1 : 0;
				reached.nearWin += old.near_win_open ? 1 : 0;
				reached.blocked += row.server.blocked ? 1 : 0;
			}
		}
		for (const [counter, steps] of Object.entries(reached)) {
			expect(steps, counter).toBeGreaterThan(0);
		}
	});
});
