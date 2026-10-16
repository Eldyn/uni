import { describe, expect, it } from "vitest";
import allowlist from "./identicalTextsAllowlist.json";
import { SHIPPED_LOCALES } from "./coreTextsHarness";

/**
 * A non-English text must differ from the English text, or the allowlist must
 * say why it may stay the same. A text that equals English reads like a
 * missing translation.
 */

const textsFiles = import.meta.glob<Record<string, string>>(
	"../../../../../mods/uni/texts/*.json",
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

function textsOf(locale: string): Record<string, string> {
	const path = Object.keys(textsFiles).find((file) => file.endsWith(`/${locale}.json`));
	if (path === undefined) throw new Error(`missing texts ${locale}`);
	return textsFiles[path];
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

function identicalKeys(locale: string): string[] {
	const english = textsOf(ENGLISH);
	const own = textsOf(locale);
	return Object.keys(english).filter((key) => own[key] === english[key]);
}

describe("no non-English text falls back to the English text", () => {
	for (const locale of NON_ENGLISH) {
		it(`${locale}: every text equal to English is on the allowlist`, () => {
			const unexplained = identicalKeys(locale).filter((key) => !allowedReason(locale, key));
			expect(unexplained).toEqual([]);
		});
	}

	it("keeps the allowlist free of stale items", () => {
		const identical = new Set(
			NON_ENGLISH.flatMap((locale) => identicalKeys(locale).map((key) => `${locale}|${key}`))
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
				expect(textsOf(locale)[key], `${locale} ${key}`).not.toBe(textsOf(ENGLISH)[key]);
			}
		}
	});
});
