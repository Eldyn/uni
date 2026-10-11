import { describe, expect, it } from "vitest";
import { buildLogLines } from "../../chat/playLog/logBuilder";
import { resolveLogText } from "../../chat/playLog/logText";
import type { LogEvent, StreakValues } from "../../chat/playLog/logEvent";
import { prepareCoreTexts } from "../glossary/coreTextsHarness";
import { SHIPPED_LOCALES, codeKeyOf, legacyKeyOf, templateOf } from "./logGoldenSupport";

/**
 * The golden files hold the play log texts as they were BEFORE they moved into
 * the `uni` mod. They never change by default. `UNI_UPDATE_GOLDEN=1` rewrites
 * them; do that only for an intended text change.
 *
 * The golden is keyed by the legacy key name. `fallbackToEnglish` is true when
 * a non-English locale had no text of its own for the key (missing, or the
 * same bytes as the English text).
 */

const GOLDEN_DIR_FROM_REPO = "tests/fixtures/golden_log_texts";
const SELECTION_FILE = "selection";
const SEQUENCE_COUNT = 64;

const LEGACY_KEYS = [
	"log_skip",
	"log_skip_again",
	"log_skip_again_2",
	"log_skip_third",
	"log_skip_streak",
	"log_skip_streak_2",
	"log_reverse",
	"log_reverse_back",
	"log_reverse_back_2",
	"log_reverse_again",
	"log_reverse_spin",
	"log_reverse_spin_2",
	"log_draw_first_1",
	"log_draw_first_2",
	"log_draw_generic_1",
	"log_draw_generic_2",
	"log_draw_generic_3",
	"log_draw_many_1",
	"log_draw_many_2",
	"log_draw_big_hand_1",
	"log_draw_big_hand_2",
	"log_draw_large_1",
	"log_blocked_1",
	"log_blocked_2",
	"log_play",
	"log_keep_drawn",
	"log_reshuffle_1",
	"log_reshuffle_2",
	"log_reshuffle_3",
	"log_wild_reaction_1",
	"log_wild_reaction_first_2",
	"log_wild_reaction_second_1",
	"log_wild_reaction_second_2",
	"log_wild_reaction_third_1",
	"log_wild_reaction_third_2",
	"log_auto_play_1",
	"log_auto_play_2",
	"log_near_win_1",
	"log_near_win_2",
	"log_near_win_3",
	"log_win_1",
	"log_win_2",
	"log_finish_1",
	"log_finish_2",
	"log_left_behind_1",
	"log_left_behind_2",
	"log_color_red",
	"log_color_yellow",
	"log_color_green",
	"log_color_blue"
];

const SAMPLE_PARAMS = {
	name: "Ann",
	kind: "uni:red_draw2",
	color: "red",
	prevColor: "blue",
	count: 2,
	amount: 2,
	total: 4,
	handSize: 5,
	drawRun: 3,
	victim: "Bob",
	blocker: "Bob",
	target: "Ann",
	place: 1
};

interface GoldenEntry {
	template: string;
	resolved: string;
	fallbackToEnglish: boolean;
}
type GoldenLocale = Record<string, GoldenEntry>;
type GoldenSelection = Record<string, string[]>;

const NO_STREAKS: StreakValues = {
	skipRun: 0,
	reverseRun: 0,
	drawRun: 0,
	totalDraws: 0,
	wildRun: 0,
	lastWildColor: null,
	prevWildColor: null
};

interface Scenario {
	event: (seq: number) => LogEvent;
	streak?: Partial<StreakValues>;
}

const drawEvent = (seq: number, extra: Partial<Extract<LogEvent, { kind: "draw" }>>): LogEvent => ({
	kind: "draw",
	seq,
	player: "Ann",
	count: 1,
	penalty: false,
	...extra
});

const SCENARIOS: Record<string, Scenario> = {
	play: {
		event: (seq) => ({ kind: "play", seq, player: "Ann", cardKind: "uni:red_5", color: "red" })
	},
	auto_play: {
		event: (seq) => ({ kind: "auto_play", seq, player: "Ann", cardKind: "uni:red_5", color: "red" })
	},
	skip_one: { event: (seq) => ({ kind: "skip", seq, player: "Ann" }), streak: { skipRun: 1 } },
	skip_two: { event: (seq) => ({ kind: "skip", seq, player: "Ann" }), streak: { skipRun: 2 } },
	skip_three: { event: (seq) => ({ kind: "skip", seq, player: "Ann" }), streak: { skipRun: 3 } },
	skip_streak: { event: (seq) => ({ kind: "skip", seq, player: "Ann" }), streak: { skipRun: 4 } },
	reverse_one: { event: (seq) => ({ kind: "reverse", seq }), streak: { reverseRun: 1 } },
	reverse_two: { event: (seq) => ({ kind: "reverse", seq }), streak: { reverseRun: 2 } },
	reverse_three: { event: (seq) => ({ kind: "reverse", seq }), streak: { reverseRun: 3 } },
	reverse_streak: { event: (seq) => ({ kind: "reverse", seq }), streak: { reverseRun: 4 } },
	draw_generic: { event: (seq) => drawEvent(seq, {}), streak: { totalDraws: 2, drawRun: 1 } },
	draw_first: { event: (seq) => drawEvent(seq, {}), streak: { totalDraws: 1 } },
	draw_many: { event: (seq) => drawEvent(seq, {}), streak: { totalDraws: 9, drawRun: 4 } },
	draw_big_hand: { event: (seq) => drawEvent(seq, { handSize: 12 }), streak: { totalDraws: 9 } },
	draw_large: { event: (seq) => drawEvent(seq, { count: 4 }), streak: { totalDraws: 9 } },
	blocked: { event: (seq) => ({ kind: "blocked", seq, player: "Ann", blocker: "Bob" }) },
	reshuffle: { event: (seq) => ({ kind: "reshuffle", seq }) },
	keep: { event: (seq) => ({ kind: "keep", seq, player: "Ann" }) },
	near_win: { event: (seq) => ({ kind: "near_win", seq, player: "Ann" }) },
	win: { event: (seq) => ({ kind: "win", seq, player: "Ann" }) },
	finished: {
		event: (seq) => ({ kind: "elimination", seq, player: "Ann", place: 2, finished: true })
	},
	left_behind: {
		event: (seq) => ({ kind: "elimination", seq, player: "Ann", place: 4, finished: false })
	},
	wild_first: {
		event: (seq) => ({ kind: "wild", seq, player: "Ann", color: "red" }),
		streak: { wildRun: 1 }
	},
	wild_second: {
		event: (seq) => ({ kind: "wild", seq, player: "Ann", color: "red" }),
		streak: { wildRun: 2, prevWildColor: "green" }
	},
	wild_third: {
		event: (seq) => ({ kind: "wild", seq, player: "Ann", color: "red" }),
		streak: { wildRun: 3, prevWildColor: "green" }
	}
};

function renderSelection(): GoldenSelection {
	const selection: GoldenSelection = {};
	for (const [name, scenario] of Object.entries(SCENARIOS)) {
		selection[name] = Array.from({ length: SEQUENCE_COUNT }, (_, seq) =>
			buildLogLines(scenario.event(seq), { ...NO_STREAKS, ...scenario.streak })
				.map((line) => legacyKeyOf(line.key))
				.join(",")
		);
	}
	return selection;
}

function renderLocale(locale: string): GoldenLocale {
	const entries: GoldenLocale = {};
	for (const legacyKey of LEGACY_KEYS) {
		const template = templateOf(locale, legacyKey);
		const english = templateOf("en", legacyKey);
		entries[legacyKey] = {
			template: template ?? "",
			resolved: resolveLogText(codeKeyOf(legacyKey), SAMPLE_PARAMS, { locale }) ?? "",
			fallbackToEnglish: locale !== "en" && (template === undefined || template === english)
		};
	}
	return entries;
}

const goldenFiles = import.meta.glob<unknown>(
	"../../../../../tests/fixtures/golden_log_texts/*.json",
	{
		eager: true,
		import: "default"
	}
);

function storedFor<T>(name: string): T | undefined {
	const key = Object.keys(goldenFiles).find((path) => path.endsWith(`/${name}.json`));
	return key === undefined ? undefined : (goldenFiles[key] as T);
}

const nodeModule = (name: string) => import(/* @vite-ignore */ `node:${name}`);
const updateRequested = (globalThis as { process?: { env?: Record<string, string> } }).process?.env
	?.UNI_UPDATE_GOLDEN;

async function writeGolden(name: string, content: unknown): Promise<void> {
	const { writeFileSync } = await nodeModule("fs");
	const { dirname, resolve } = await nodeModule("path");
	const { fileURLToPath } = await nodeModule("url");
	const repoRoot = resolve(dirname(fileURLToPath(import.meta.url)), "../../../../..");
	writeFileSync(
		resolve(repoRoot, GOLDEN_DIR_FROM_REPO, `${name}.json`),
		JSON.stringify(content, null, 2) + "\n"
	);
}

describe("play log texts equal the stored golden in every locale", () => {
	it("lists each log key once", () => {
		expect(LEGACY_KEYS.length).toBe(50);
		expect(new Set(LEGACY_KEYS).size).toBe(LEGACY_KEYS.length);
	});

	it("selects only listed keys", () => {
		const picked = Object.values(renderSelection())
			.flat()
			.flatMap((cell) => cell.split(","));
		expect(picked.filter((key) => !LEGACY_KEYS.includes(key))).toEqual([]);
	});

	for (const locale of SHIPPED_LOCALES) {
		it(`locale ${locale}`, async () => {
			await prepareCoreTexts(locale);
			const rendered = renderLocale(locale);
			if (updateRequested) {
				await writeGolden(locale, rendered);
				return;
			}
			const stored = storedFor<GoldenLocale>(locale);
			expect(stored, `missing golden file for ${locale}`).toBeDefined();
			expect(Object.keys(rendered)).toEqual(Object.keys(stored!));
			for (const key of Object.keys(stored!)) {
				const expected = stored![key];
				if (expected.fallbackToEnglish) continue;
				expect(rendered[key], `${locale} ${key}`).toEqual(expected);
			}
		});
	}
});

describe("the variant picked for a sequence number stays the same", () => {
	it("matches the stored selection for sequence 0 to 63", async () => {
		await prepareCoreTexts("en");
		const rendered = renderSelection();
		if (updateRequested) {
			await writeGolden(SELECTION_FILE, rendered);
			return;
		}
		expect(rendered).toEqual(storedFor<GoldenSelection>(SELECTION_FILE));
	});
});
