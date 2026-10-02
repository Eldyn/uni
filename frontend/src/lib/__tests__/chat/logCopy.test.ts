import { describe, expect, it } from "vitest";
import { REACTION_RULES } from "../../chat/playLog/logBuilder";
import { resolveLogText } from "../../chat/playLog/logText";
import { getGlossaryEntry } from "$lib/glossary/glossary";
import type { LogEvent, StreakInfo } from "../../chat/playLog/logEvent";
import type { ReactionContext } from "../../chat/playLog/reactionContext";

const localeCatalogs = import.meta.glob<Record<string, string>>("../../../../messages/*.json", {
	eager: true,
	import: "default"
});

const PALETTE_COLORS = ["red", "yellow", "green", "blue"];
const ALLOWED_EFFECTS = ["shake", "undulate", "shine"];
const HEX_COLOR_RE = /^#[0-9a-fA-F]{3,8}$/;
const PLACEHOLDER_RE = /\{[^}]*\}/g;
const TAG_RE = /\[(c|fx|k)=([^\]]*)\]|\[\/(c|fx|k)\]/g;

const PLACEHOLDER_SAMPLES: Record<string, string> = {
	name: "Ann",
	kind: "vanilla:red_draw2",
	color: "red",
	colorName: "red",
	prevColor: "blue",
	prevColorName: "blue",
	cardName: "Draw Two",
	count: "2",
	amount: "2",
	total: "4",
	handSize: "5",
	drawRun: "3",
	victim: "Bob",
	place: "1"
};

const EMPTY_STREAK: StreakInfo = {
	skipRun: 0,
	reverseRun: 0,
	drawRun: 0,
	totalDraws: 0,
	wildRun: 0,
	lastWildColor: null,
	prevWildColor: null,
	nearWinTarget: null,
	nearWinOpen: false
};

const SAMPLE_EVENTS: Record<string, LogEvent> = {
	skip: { kind: "skip", seq: 1, player: "Ann" },
	reverse: { kind: "reverse", seq: 2 },
	draw: {
		kind: "draw",
		seq: 3,
		player: "Ann",
		count: 2,
		penalty: false,
		amount: 4,
		total: 8,
		handSize: 12,
		victim: "Bob"
	},
	play: { kind: "play", seq: 1, player: "Ann", cardKind: "vanilla:red_5", color: "red" },
	reshuffle: { kind: "reshuffle", seq: 4 },
	wild: { kind: "wild", seq: 5, player: "Ann", color: "red" },
	auto_play: {
		kind: "auto_play",
		seq: 6,
		player: "Ann",
		cardKind: "vanilla:red_5",
		color: "red"
	},
	near_win: { kind: "near_win", seq: 7, player: "Ann" },
	win: { kind: "win", seq: 1, player: "Ann" },
	elimination: { kind: "elimination", seq: 2, player: "Ann", place: 2 }
};

const ruleCases = Object.entries(SAMPLE_EVENTS).flatMap(([, event]) => {
	const ctx: ReactionContext = { seq: event.seq, event, ...EMPTY_STREAK };
	return REACTION_RULES.filter((rule) => rule.match(ctx)).flatMap((rule) =>
		Object.values(rule.pools)
			.flat()
			.map((variant) => ({ key: variant.key, params: rule.params(ctx) }))
	);
});

const reactionKeys = Array.from(
	new Set(
		REACTION_RULES.flatMap((rule) =>
			Object.values(rule.pools)
				.flat()
				.map((variant) => variant.key)
		)
	)
);
const colorNameKeys = PALETTE_COLORS.map((color) => `log_color_${color}`);
const copyKeys = [...reactionKeys, ...colorNameKeys];

/** Returns a problem description, or null when every tag is known, resolvable, and closed in order. */
function tagProblem(text: string): string | null {
	const open: string[] = [];
	for (const [, openName, value, closeName] of text.matchAll(TAG_RE)) {
		if (closeName) {
			if (open.pop() !== closeName) return `stray [/${closeName}]`;
			continue;
		}
		if (openName === "c") {
			if (!PALETTE_COLORS.includes(value) && !HEX_COLOR_RE.test(value))
				return `unknown [c=${value}]`;
		} else if (openName === "fx") {
			if (!ALLOWED_EFFECTS.includes(value)) return `unknown [fx=${value}]`;
		} else if (openName === "k") {
			if (!getGlossaryEntry(value)) return `unresolved [k=${value}]`;
		}
		open.push(openName);
	}
	return open.length > 0 ? `unclosed [${open.join(", ")}]` : null;
}

/** Substitutes every placeholder with a value that satisfies the tag grammar. */
function withSampleParams(text: string): string {
	return text.replace(PLACEHOLDER_RE, (match) => PLACEHOLDER_SAMPLES[match.slice(1, -1)] ?? "x");
}

describe("play log copy renders for every REACTION_RULES pool key", () => {
	it.each(ruleCases)("$key resolves to clean, balanced copy", ({ key, params }) => {
		const text = resolveLogText(key, params);
		expect(text, `${key} is missing from the catalog`).toBeTruthy();
		expect(text?.trim().length).toBeGreaterThan(0);
		expect(text?.match(PLACEHOLDER_RE) ?? []).toEqual([]);
		expect(tagProblem(text ?? "")).toBeNull();
	});

	it.each(PALETTE_COLORS)("names the %s wild colour and tags it with its id", (color) => {
		const text = resolveLogText("log_wild_reaction_1", { name: "Ann", color });
		expect(text).toContain(`[c=${color}]`);
		expect(resolveLogText(`log_color_${color}`, {})?.trim()).toBeTruthy();
	});

	it("keeps a colorName the caller already supplied", () => {
		const text = resolveLogText("log_wild_reaction_1", {
			name: "Ann",
			color: "red",
			colorName: "crimson"
		});
		expect(text).toContain("[c=red]crimson[/c]");
	});

	it("falls back to the raw id for an unknown colour", () => {
		const text = resolveLogText("log_wild_reaction_1", { name: "Ann", color: "purple" });
		expect(text).toContain("[c=purple]purple[/c]");
	});

	it("renders the card keyword and localized name in a play line", () => {
		const text = resolveLogText(
			"log_play",
			{ name: "Ann", kind: "vanilla:red_draw2", color: "red" },
			{ locale: "en" }
		);
		expect(text).toContain("[k=vanilla:red_draw2]");
		expect(text).toContain("[c=red]");
		expect(text).toContain("Draw Two");
	});

	it("returns null for a key the catalog lacks", () => {
		expect(resolveLogText("log_does_not_exist", {})).toBeNull();
	});
});

describe("play log copy in every locale", () => {
	const english = Object.entries(localeCatalogs).find(([path]) => path.endsWith("/en.json"))?.[1];
	const placeholdersOf = (text: string) => [...(text.match(PLACEHOLDER_RE) ?? [])].sort();

	it.each(Object.keys(localeCatalogs))("%s has every key with the English params", (path) => {
		const catalog = localeCatalogs[path];
		for (const key of copyKeys) {
			const text = catalog[key];
			expect(text?.trim(), `${path} lacks ${key}`).toBeTruthy();
			expect(placeholdersOf(text), `${path} ${key} params`).toEqual(
				placeholdersOf(english?.[key] ?? "")
			);
			expect(tagProblem(withSampleParams(text)), `${path} ${key} tags`).toBeNull();
		}
	});
});
