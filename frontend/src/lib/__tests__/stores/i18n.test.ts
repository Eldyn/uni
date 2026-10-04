import { describe, it, expect, vi, beforeEach } from "vitest";

const paraglideState = { locale: "en" };
const loaderState = { loaded: new Set(["en", "it"]), loadLocale: vi.fn() };

vi.mock("$lib/paraglide/runtime.js", () => ({
	locales: ["en", "it"],
	getLocale: () => paraglideState.locale,
	setLocale: vi.fn((locale: string, _options?: { reload?: boolean }) => {
		paraglideState.locale = locale;
	})
}));

vi.mock("$lib/utils/localeLoader", () => ({
	isLocaleLoaded: (locale: string) => loaderState.loaded.has(locale),
	loadLocale: (locale: string) => loaderState.loadLocale(locale)
}));

beforeEach(() => {
	loaderState.loaded = new Set(["en", "it"]);
	loaderState.loadLocale = vi.fn();
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

	it("fetches an unloaded locale before switching to it", async () => {
		loaderState.loaded = new Set(["en"]);
		loaderState.loadLocale = vi.fn(async (locale: string) => {
			loaderState.loaded.add(locale);
		});
		const { storeI18n } = await import("$lib/stores/i18n.svelte");

		const switching = storeI18n.setLocale("it");
		expect(storeI18n.locale).toBe("en");

		await switching;
		expect(loaderState.loadLocale).toHaveBeenCalledWith("it");
		expect(storeI18n.locale).toBe("it");
	});
});
