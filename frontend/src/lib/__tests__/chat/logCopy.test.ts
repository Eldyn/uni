import { describe, expect, it } from "vitest";
import { REACTION_RULES } from "../../chat/playLog/logBuilder";
import { resolveLogText } from "../../chat/playLog/logText";
import type { LogEvent, StreakInfo } from "../../chat/playLog/logEvent";
import type { ReactionRule } from "../../chat/playLog/reactionRules";

const localeCatalogs = import.meta.glob<Record<string, string>>("../../../../messages/*.json", {
	eager: true,
	import: "default"
});

const PALETTE_COLORS = ["red", "yellow", "green", "blue"];
const ALLOWED_EFFECTS = ["shake"];
const PLACEHOLDER_RE = /\{[^}]*\}/g;
const TAG_RE = /\[(c|fx)=([^\]]*)\]|\[\/(c|fx)\]/g;

const EMPTY_STREAK: StreakInfo = {
	skipRun: 0,
	reverseRun: 0,
	stackedDebt: 0,
	drawRun: 0,
	totalDraws: 0
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
	play: { kind: "play", seq: 1, player: "Ann", cardKind: "vanilla:red_5", color: "red" }
};

const ruleCases = Object.entries(SAMPLE_EVENTS).flatMap(([kind, event]) => {
	const rule: ReactionRule | undefined = REACTION_RULES[kind as keyof typeof REACTION_RULES];
	if (!rule) return [];
	return Object.values(rule.pools)
		.flat()
		.map((variant) => ({ key: variant.key, params: rule.params(event, EMPTY_STREAK) }));
});

const colorNameKeys = PALETTE_COLORS.map((color) => `log_color_${color}`);
const copyKeys = [...ruleCases.map(({ key }) => key), ...colorNameKeys];

/** Returns a problem description, or null when every tag is known and closed in order. */
function tagProblem(text: string): string | null {
	const open: string[] = [];
	for (const [, openName, value, closeName] of text.matchAll(TAG_RE)) {
		if (closeName) {
			if (open.pop() !== closeName) return `stray [/${closeName}]`;
			continue;
		}
		const allowed = openName === "c" ? PALETTE_COLORS : ALLOWED_EFFECTS;
		if (!allowed.includes(value)) return `unknown [${openName}=${value}]`;
		open.push(openName);
	}
	return open.length > 0 ? `unclosed [${open.join(", ")}]` : null;
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
		const text = resolveLogText("log_wild", { name: "Ann", color });
		expect(text).toContain(`[c=${color}]`);
		expect(resolveLogText(`log_color_${color}`, {})?.trim()).toBeTruthy();
	});

	it("keeps a colorName the caller already supplied", () => {
		const text = resolveLogText("log_wild", { name: "Ann", color: "red", colorName: "crimson" });
		expect(text).toContain("[c=red]crimson[/c]");
	});

	it("falls back to the raw id for an unknown colour", () => {
		const text = resolveLogText("log_wild", { name: "Ann", color: "purple" });
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
			const sampleText = text.replace(/\{color\}/g, PALETTE_COLORS[0]);
			expect(tagProblem(sampleText), `${path} ${key} tags`).toBeNull();
		}
	});
});
