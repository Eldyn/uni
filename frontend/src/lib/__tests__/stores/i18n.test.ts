import { describe, it, expect, vi, beforeEach } from "vitest";

const paraglideState = { locale: "en" };

vi.mock("$lib/paraglide/runtime.js", () => ({
	locales: ["en", "it"],
	getLocale: () => paraglideState.locale,
	setLocale: vi.fn((locale: string, _options?: { reload?: boolean }) => {
		paraglideState.locale = locale;
	})
}));

beforeEach(() => {
	paraglideState.locale = "en";
	document.documentElement.lang = "";
	vi.resetModules();
});

describe("storeI18n", () => {
	it("reads the current locale from Paraglide", async () => {
		const { storeI18n } = await import("$lib/stores/i18n.svelte");
		expect(storeI18n.locale).toBe("en");
	});

	it("setLocale updates Paraglide's locale, storeI18n.locale and the document lang attribute", async () => {
		const { storeI18n } = await import("$lib/stores/i18n.svelte");

		storeI18n.setLocale("it");

		expect(storeI18n.locale).toBe("it");
		expect(document.documentElement.lang).toBe("it");
	});
});
