import { describe, expect, it, beforeEach } from "vitest";
import { resolveLogText } from "$lib/presentation/logText";
import { getGlossaryEntry } from "$lib/glossary/glossary";
import { prepareCoreTexts } from "../glossary/coreTextsHarness";
import { PALETTE_COLORS, tagProblem as richTagProblem } from "./richTagProblems";

beforeEach(async () => {
	await prepareCoreTexts("en");
});

const presentationFiles = import.meta.glob<Record<string, unknown>>(
	"../../../../../mods/uni/presentations/*.json",
	{ eager: true, import: "default" }
);

const localeCatalogs = import.meta.glob<Record<string, string>>(
	"../../../../../mods/uni/texts/*.json",
	{
		eager: true,
		import: "default"
	}
);

const PLACEHOLDER_RE = /\{[^}]*\}/g;

const PLACEHOLDER_SAMPLES: Record<string, string> = {
	name: "Ann",
	kind: "uni:red_draw2",
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
	blocker: "Bob",
	target: "Ann",
	place: "1"
};

interface ShippedCase {
	key: string;
	params: Record<string, string>;
}

/** The log copy the shipped bindings can post: one case per variant key, with
 *  a sample for every param the case declares. */
function shippedCases(): ShippedCase[] {
	const cases: ShippedCase[] = [];
	for (const presentation of Object.values(presentationFiles)) {
		const binding = (presentation as Record<string, unknown>)["uni:binding"] as {
			"uni:cases": {
				"uni:present": { "uni:log"?: { "uni:variants"?: string[]; "uni:params"?: object } };
			}[];
		};
		for (const logCase of binding["uni:cases"]) {
			const log = logCase["uni:present"]["uni:log"];
			if (log === undefined || log["uni:variants"] === undefined) continue;
			const params = Object.fromEntries(
				Object.keys(log["uni:params"] ?? {}).map((name) => [
					name,
					PLACEHOLDER_SAMPLES[name] ?? "Bob"
				])
			);
			for (const key of log["uni:variants"]) cases.push({ key, params });
		}
	}
	return cases;
}

const ruleCases = shippedCases();
const reactionKeys = [...new Set(ruleCases.map((ruleCase) => ruleCase.key))];
const colorNameKeys = PALETTE_COLORS.map((color) => `uni:log.color_${color}`);
const copyKeys = [...reactionKeys, ...colorNameKeys];

/** Substitutes every placeholder with a value that satisfies the tag grammar. */
function withSampleParams(text: string): string {
	return text.replace(PLACEHOLDER_RE, (match) => PLACEHOLDER_SAMPLES[match.slice(1, -1)] ?? "x");
}

const tagProblem = (text: string) => richTagProblem(text, (id) => !!getGlossaryEntry(id));

describe("tagProblem fx param validation", () => {
	it.each(["[fx=shake]x[/fx]", "[fx=shake:2]x[/fx]", "[fx=shake:2,3]x[/fx]"])(
		"accepts %s",
		(text) => {
			expect(tagProblem(text)).toBeNull();
		}
	);

	it.each([
		"[fx=shake:4]x[/fx]",
		"[fx=shake: 2]x[/fx]",
		"[fx=shake:2,]x[/fx]",
		"[fx=shake:2:3]x[/fx]",
		"[fx=bogus:2]x[/fx]"
	])("rejects %s", (text) => {
		expect(tagProblem(text)).not.toBeNull();
	});
});

describe("play log copy renders for every shipped variant key", () => {
	it.each(ruleCases)("$key resolves to clean, balanced copy", ({ key, params }) => {
		const text = resolveLogText(key, params);
		expect(text, `${key} is missing from the catalog`).toBeTruthy();
		expect(text?.trim().length).toBeGreaterThan(0);
		expect(text?.match(PLACEHOLDER_RE) ?? []).toEqual([]);
		expect(tagProblem(text ?? "")).toBeNull();
	});

	it.each(PALETTE_COLORS)("names the %s wild colour and tags it with its id", (color) => {
		const text = resolveLogText("uni:log.wild_reaction_1", { name: "Ann", color });
		expect(text).toContain(`[c=${color}]`);
		expect(resolveLogText(`uni:log.color_${color}`, {})?.trim()).toBeTruthy();
	});

	it("keeps a colorName the caller already supplied", () => {
		const text = resolveLogText("uni:log.wild_reaction_1", {
			name: "Ann",
			color: "red",
			colorName: "crimson"
		});
		expect(text).toContain("[c=red]crimson[/c]");
	});

	it("falls back to the raw id for an unknown colour", () => {
		const text = resolveLogText("uni:log.wild_reaction_1", { name: "Ann", color: "purple" });
		expect(text).toContain("[c=purple]purple[/c]");
	});

	it("names both the previous and current wild colours", () => {
		const text = resolveLogText(
			"uni:log.wild_reaction_second_2",
			{ name: "Ann", color: "red", prevColor: "green" },
			{ locale: "en" }
		);
		expect(text).toContain("[c=green]green[/c]");
		expect(text).toContain("[c=red]red[/c]");
		expect(text?.match(PLACEHOLDER_RE) ?? []).toEqual([]);
	});

	it("keeps a prevColorName the caller already supplied", () => {
		const text = resolveLogText("uni:log.wild_reaction_second_2", {
			name: "Ann",
			color: "red",
			prevColor: "green",
			prevColorName: "emerald"
		});
		expect(text).toContain("[c=green]emerald[/c]");
		expect(text).toContain("[c=red]red[/c]");
	});

	it("renders the card keyword and localized name in a play line", () => {
		const text = resolveLogText(
			"uni:log.play",
			{ name: "Ann", kind: "uni:red_draw2", color: "red" },
			{ locale: "en" }
		);
		expect(text).toContain("[k=uni:red_draw2]");
		expect(text).toContain("[c=red]");
		expect(text).toContain("Draw Two");
	});

	it("returns null for a key the catalog lacks", () => {
		expect(resolveLogText("uni:log.does_not_exist", {})).toBeNull();
	});
});

describe("play log copy in every locale", () => {
	const english = Object.entries(localeCatalogs).find(([path]) => path.endsWith("/en.json"))?.[1];
	const placeholdersOf = (text: string) => [...(text.match(PLACEHOLDER_RE) ?? [])].sort();

	it.each(Object.keys(localeCatalogs))("%s has every log key with the English params", (path) => {
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
