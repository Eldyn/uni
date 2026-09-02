import { describe, it, expect, vi, beforeEach } from "vitest";

const paraglideState = { locale: "en" };

vi.mock("$lib/paraglide/runtime.js", () => ({
	locales: ["en", "it"],
	getLocale: () => paraglideState.locale,
	setLocale: vi.fn((locale: string) => {
		paraglideState.locale = locale;
	})
}));

beforeEach(() => {
	paraglideState.locale = "en";
	vi.resetModules();
});

describe("locale restore", () => {
	it("applies a persisted locale that differs from the browser default", async () => {
		window.localStorage.setItem("locale", "it");
		const { restorePersistedLocale } = await import("$lib/stores/i18n.svelte");

		restorePersistedLocale();

		expect(paraglideState.locale).toBe("it");
	});

	it("ignores a persisted value that isn't a supported locale", async () => {
		window.localStorage.setItem("locale", "fr");
		const { restorePersistedLocale } = await import("$lib/stores/i18n.svelte");

		restorePersistedLocale();

		expect(paraglideState.locale).toBe("en");
	});

	it("does nothing when nothing is persisted", async () => {
		window.localStorage.removeItem("locale");
		const { restorePersistedLocale } = await import("$lib/stores/i18n.svelte");

		restorePersistedLocale();

		expect(paraglideState.locale).toBe("en");
	});
});
