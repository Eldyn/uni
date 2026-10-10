import { describe, expect, it } from "vitest";
import { formatMessage } from "$lib/i18n/messageFormat";
import { SHIPPED_LOCALES } from "./coreTextsHarness";

const textsFiles = import.meta.glob<Record<string, unknown>>(
	"../../../../../mods/uni/texts/*.json",
	{ eager: true, import: "default" }
);
const messageFiles = import.meta.glob<Record<string, string>>("../../../../messages/*.json", {
	eager: true,
	import: "default"
});

function textsOf(locale: string): Record<string, unknown> {
	const key = Object.keys(textsFiles).find((path) => path.endsWith(`/${locale}.json`));
	if (key === undefined) throw new Error(`missing texts file ${locale}`);
	return textsFiles[key];
}

const PLACEHOLDER = /\{\s*([A-Za-z0-9_]+)\s*[,}]/g;
const PLURAL_BLOCK = /\{\s*[A-Za-z0-9_]+\s*,\s*plural\s*,/;
const PLURAL_BRANCH_NAME = /(?:^|[\s}])([a-z]+|=\d+)\s*\{/g;

function placeholderNames(text: string): string[] {
	return [...text.matchAll(PLACEHOLDER)].map((match) => match[1]).sort();
}

describe("the uni texts files", () => {
	it("exist for exactly the shipped locales", () => {
		const found = Object.keys(textsFiles)
			.map((path) => path.slice(path.lastIndexOf("/") + 1, -".json".length))
			.sort();
		expect(found).toEqual([...SHIPPED_LOCALES].sort());
	});

	for (const locale of SHIPPED_LOCALES) {
		it(`${locale}: holds the keys of en, no missing and no extra`, () => {
			const english = Object.keys(textsOf("en")).sort();
			expect(Object.keys(textsOf(locale)).sort()).toEqual(english);
		});

		it(`${locale}: every key is in the uni namespace and every text is a non-empty string`, () => {
			for (const [key, value] of Object.entries(textsOf(locale))) {
				expect(key.startsWith("uni:"), key).toBe(true);
				expect(typeof value, key).toBe("string");
				expect((value as string).length, key).toBeGreaterThan(0);
			}
		});

		it(`${locale}: the placeholders of every key equal the placeholders of en`, () => {
			const english = textsOf("en") as Record<string, string>;
			for (const [key, value] of Object.entries(textsOf(locale) as Record<string, string>)) {
				expect(placeholderNames(value), `${locale} ${key}`).toEqual(placeholderNames(english[key]));
			}
		});

		it(`${locale}: every text formats with no warning and no error`, () => {
			const problems: string[] = [];
			const report = {
				warn: (message: string) => problems.push(`warn ${message}`),
				error: (message: string) => problems.push(`error ${message}`)
			};
			for (const [key, value] of Object.entries(textsOf(locale) as Record<string, string>)) {
				formatMessage(value, {}, { locale, report });
			}
			expect(problems).toEqual([]);
		});

		it(`${locale}: a plural uses only branches that the locale knows, and has other`, () => {
			const categories = new Set<string>(
				new Intl.PluralRules(locale).resolvedOptions().pluralCategories
			);
			for (const [key, value] of Object.entries(textsOf(locale) as Record<string, string>)) {
				if (!PLURAL_BLOCK.test(value)) continue;
				const branchText = value.slice(value.search(PLURAL_BLOCK));
				const names = [...branchText.matchAll(PLURAL_BRANCH_NAME)].map((match) => match[1]);
				expect(names, `${locale} ${key}`).toContain("other");
				for (const name of names) {
					expect(categories.has(name), `${locale} ${key} branch ${name}`).toBe(true);
				}
			}
		});
	}
});

describe("the interface catalogs no longer hold the moved texts", () => {
	const MOVED_KEY = /^(glossary_(?!tag_|title$)|card_desc_|card_title_)/;

	it("reads the message files", () => {
		expect(Object.keys(messageFiles).length).toBe(SHIPPED_LOCALES.length + 1);
	});

	for (const [path, catalog] of Object.entries(messageFiles)) {
		it(`${path.slice(path.lastIndexOf("/") + 1)} holds no glossary or card description key`, () => {
			expect(Object.keys(catalog).filter((key) => MOVED_KEY.test(key))).toEqual([]);
		});
	}

	it("keeps the interface labels of the glossary pills", () => {
		for (const catalog of Object.values(messageFiles)) {
			for (const key of [
				"glossary_tag_rule",
				"glossary_tag_action",
				"glossary_tag_flow",
				"glossary_tag_pile"
			]) {
				expect(catalog[key], key).toBeTruthy();
			}
		}
	});
});
