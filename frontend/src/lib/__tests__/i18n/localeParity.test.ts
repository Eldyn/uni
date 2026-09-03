import { describe, it, expect } from "vitest";
import en from "../../../../messages/en.json";
import itLocale from "../../../../messages/it.json";

describe("locale catalogue parity", () => {
	it("every English key has an Italian translation", () => {
		for (const key of Object.keys(en)) {
			if (key === "$schema") continue;
			expect(itLocale, `missing it.json entry for "${key}"`).toHaveProperty(key);
		}
	});

	it("has no Italian keys unknown to English", () => {
		for (const key of Object.keys(itLocale)) {
			if (key === "$schema") continue;
			expect(en, `it.json has stray key "${key}" not present in en.json`).toHaveProperty(key);
		}
	});
});
