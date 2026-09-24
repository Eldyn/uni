/**
 * @file i18n.svelte.ts
 * @brief One reactive place to read and change the active locale.
 *
 * Wraps Paraglide's runtime rather than being read directly, so components
 * don't each re-implement "set document.lang on switch" — see storeAudio
 * for the equivalent pattern with audio settings. Persistence across visits
 * is handled entirely by Paraglide's own cookie + localStorage strategy
 * chain (see the `strategy` option passed to `paraglideVitePlugin` in
 * vite.config.js) — this store does not persist anything itself.
 */

import { getLocale, setLocale as paraglideSetLocale, locales } from "$lib/paraglide/runtime.js";
import { storeAnalytics } from "./analytics.svelte";

// Paraglide's generated runtime.js only exposes `Locale` as a JSDoc typedef,
// not an importable TS type, so it's derived locally from the locales tuple.
type Locale = (typeof locales)[number];

class StoreI18n {
	locale = $state(getLocale());

	// "pseudo" is a dev-only smoke-test locale for spotting untranslated
	// strings; it must never be offered to end users as a real option.
	readonly locales: readonly Locale[] = (locales as readonly Locale[]).filter(
		(candidate) => candidate !== "pseudo"
	);

	/**
	 * @brief Applies the locale resolved at construction (from Paraglide's
	 * cookie/localStorage strategy chain) to the document. Call once, from
	 * App.svelte's onMount — not from the constructor, since this module is
	 * a singleton constructed at import time and `document` may not exist
	 * in every context that imports it (e.g. non-DOM test environments).
	 */
	init(): void {
		document.documentElement.lang = this.locale;
	}

	setLocale(locale: Locale): void {
		const previous = this.locale;
		paraglideSetLocale(locale, { reload: false });
		this.locale = locale;
		document.documentElement.lang = locale;
		// Explicit language choices only (not the auto-resolved initial locale).
		if (locale !== previous) {
			storeAnalytics.track("locale_change", { from: previous, to: locale });
		}
	}
}

export const storeI18n = new StoreI18n();
