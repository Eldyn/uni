import { describe, it, expect } from "vitest";
import en from "../../../../messages/en.json";
import itLocale from "../../../../messages/it.json";
import esLocale from "../../../../messages/es.json";
import deLocale from "../../../../messages/de.json";
import jaLocale from "../../../../messages/ja.json";
import koLocale from "../../../../messages/ko.json";
import zhLocale from "../../../../messages/zh.json";
import ukLocale from "../../../../messages/uk.json";
import ruLocale from "../../../../messages/ru.json";

const locales: Record<string, Record<string, string>> = {
	it: itLocale as Record<string, string>,
	es: esLocale as Record<string, string>,
	de: deLocale as Record<string, string>,
	ja: jaLocale as Record<string, string>,
	ko: koLocale as Record<string, string>,
	zh: zhLocale as Record<string, string>,
	uk: ukLocale as Record<string, string>,
	ru: ruLocale as Record<string, string>
};

describe("locale catalogue parity across all locales", () => {
	const enKeys = Object.keys(en).filter((k) => k !== "$schema");

	for (const [lang, catalog] of Object.entries(locales)) {
		it(`every English key has a ${lang} translation`, () => {
			for (const key of enKeys) {
				expect(catalog, `missing ${lang}.json entry for "${key}"`).toHaveProperty(key);
				expect(
					catalog[key].trim().length,
					`${lang}.json entry for "${key}" is empty`
				).toBeGreaterThan(0);
			}
		});

		it(`has no ${lang} keys unknown to English`, () => {
			for (const key of Object.keys(catalog)) {
				if (key === "$schema") continue;
				expect(en, `${lang}.json has stray key "${key}" not present in en.json`).toHaveProperty(
					key
				);
			}
		});
	}
});
