/**
 * @file i18n.svelte.ts
 * @brief One reactive place to read and change the active locale.
 *
 * Wraps Paraglide's runtime rather than being read directly, so components
 * don't each re-implement "persist to localStorage and set document.lang" —
 * see storeAudio for the equivalent pattern with audio settings.
 */

import { getLocale, setLocale as paraglideSetLocale, locales } from "$lib/paraglide/runtime.js";

const LOCALE_STORAGE_KEY = "locale";

class StoreI18n {
	locale = $state(getLocale());

	readonly locales = locales as readonly string[];

	constructor() {
		document.documentElement.lang = this.locale;
	}

	setLocale(locale: string): void {
		paraglideSetLocale(locale as never);
		this.locale = locale;
		localStorage.setItem(LOCALE_STORAGE_KEY, locale);
		document.documentElement.lang = locale;
	}
}

export const storeI18n = new StoreI18n();
