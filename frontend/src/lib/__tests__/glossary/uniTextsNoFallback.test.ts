import { describe, expect, it } from "vitest";
import allowlist from "./identicalTextsAllowlist.json";
import { SHIPPED_LOCALES } from "./coreTextsHarness";

/**
 * A non-English text must differ from the English text, or the allowlist must
 * say why it may stay the same. A text that equals English reads like a
 * missing translation.
 *
 * The check covers `uni` and the four standard mods, whose texts are namespaced
 * by their mod id, so a key is unique across the covered mods.
 */

const textsFiles = import.meta.glob<Record<string, string>>(
	[
		"../../../../../mods/uni/texts/*.json",
		"../../../../../mods/draw_stacking/texts/*.json",
		"../../../../../mods/jump_in/texts/*.json",
		"../../../../../mods/seven_zero/texts/*.json",
		"../../../../../mods/progressive/texts/*.json"
	],
	{ eager: true, import: "default" }
);
const goldenRewrittenFiles = import.meta.glob<Record<string, string[]>>(
	"../../../../../tests/fixtures/golden_log_texts/rewritten.json",
	{ eager: true, import: "default" }
);
const goldenLocaleFiles = import.meta.glob<Record<string, { fallbackToEnglish: boolean }>>(
	"../../../../../tests/fixtures/golden_log_texts/*.json",
	{ eager: true, import: "default" }
);

const ENGLISH = "en";
const NON_ENGLISH = SHIPPED_LOCALES.filter((locale) => locale !== ENGLISH);

/** The mod id of a texts file path. */
function modOf(path: string): string {
	const from = path.indexOf("/mods/") + "/mods/".length;
	return path.slice(from, path.indexOf("/texts/", from));
}

/** The texts of every covered mod, by mod id and then by locale. */
const textsByMod = new Map<string, Map<string, Record<string, string>>>();
for (const [path, texts] of Object.entries(textsFiles)) {
	const locale = path.slice(path.lastIndexOf("/") + 1, -".json".length);
	const byLocale = textsByMod.get(modOf(path)) ?? new Map<string, Record<string, string>>();
	byLocale.set(locale, texts);
	textsByMod.set(modOf(path), byLocale);
}

function textsOf(mod: string, locale: string): Record<string, string> {
	const found = textsByMod.get(mod)?.get(locale);
	if (found === undefined) throw new Error(`missing texts ${mod} ${locale}`);
	return found;
}

const patterns = allowlist.patterns.map((item) => ({
	...item,
	regex: new RegExp(item.keyPattern)
}));

function allowedReason(locale: string, key: string): string | undefined {
	const entry = allowlist.entries.find((item) => item.locale === locale && item.key === key);
	if (entry) return entry.reason;
	return patterns.find((item) => item.regex.test(key))?.reason;
}

function identicalKeys(mod: string, locale: string): string[] {
	const english = textsOf(mod, ENGLISH);
	const own = textsOf(mod, locale);
	return Object.keys(english).filter((key) => own[key] === english[key]);
}

/** Every text that equals English, over all covered mods. */
function identicalKeysEverywhere(locale: string): string[] {
	return [...textsByMod.keys()].flatMap((mod) => identicalKeys(mod, locale));
}

describe("no non-English text falls back to the English text", () => {
	for (const mod of textsByMod.keys()) {
		for (const locale of NON_ENGLISH) {
			it(`${mod} ${locale}: every text equal to English is on the allowlist`, () => {
				const unexplained = identicalKeys(mod, locale).filter((key) => !allowedReason(locale, key));
				expect(unexplained).toEqual([]);
			});
		}
	}

	it("keeps the allowlist free of stale items", () => {
		const identical = new Set(
			NON_ENGLISH.flatMap((locale) =>
				identicalKeysEverywhere(locale).map((key) => `${locale}|${key}`)
			)
		);
		for (const item of allowlist.entries) {
			expect(identical.has(`${item.locale}|${item.key}`), `${item.locale} ${item.key}`).toBe(true);
			expect(item.reason.length).toBeGreaterThan(0);
		}
		for (const item of patterns) {
			const used = [...identical].some((id) => item.regex.test(id.slice(id.indexOf("|") + 1)));
			expect(used, item.keyPattern).toBe(true);
			expect(item.reason.length).toBeGreaterThan(0);
		}
	});
});

describe("the texts that fell back to English before the move are rewritten", () => {
	const rewritten = Object.values(goldenRewrittenFiles)[0];

	it("lists exactly the keys that the golden flagged, per locale", () => {
		for (const locale of NON_ENGLISH) {
			const goldenPath = Object.keys(goldenLocaleFiles).find((file) =>
				file.endsWith(`/${locale}.json`)
			);
			const flagged = Object.entries(goldenLocaleFiles[goldenPath!])
				.filter(([, entry]) => entry.fallbackToEnglish)
				.map(([key]) => `uni:log.${key.slice("log_".length)}`);
			expect(rewritten[locale], locale).toEqual(flagged);
		}
	});

	it("differs from English for every listed key", () => {
		for (const locale of NON_ENGLISH) {
			for (const key of rewritten[locale]) {
				expect(textsOf("uni", locale)[key], `${locale} ${key}`).not.toBe(
					textsOf("uni", ENGLISH)[key]
				);
			}
		}
	});
});

describe("every covered mod holds the key set of en in every locale", () => {
	for (const mod of textsByMod.keys()) {
		it(`${mod}: a locale holds neither a missing nor an extra key`, () => {
			const english = Object.keys(textsOf(mod, ENGLISH));
			for (const locale of NON_ENGLISH) {
				const own = Object.keys(textsOf(mod, locale));
				expect(own.filter((key) => !english.includes(key)), `${mod} ${locale} extra`).toEqual([]);
				expect(
					english.filter((key) => !own.includes(key)),
					`${mod} ${locale} missing`
				).toEqual([]);
			}
		});
	}
});
