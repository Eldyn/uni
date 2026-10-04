/**
 * @file localeLoader.ts
 * @brief Typed access to the on-demand locale loader that lazyLocalesPlugin
 * (vite.config.js) appends to Paraglide's generated messages index.
 */

import * as generatedMessages from "$lib/paraglide/messages/_index.js";
import type { locales } from "$lib/paraglide/runtime.js";

export type Locale = (typeof locales)[number];

interface LazyLocales {
	isLocaleLoaded(locale: Locale): boolean;
	loadLocale(locale: Locale): Promise<void>;
}

const lazyLocales = generatedMessages as unknown as LazyLocales;

export const isLocaleLoaded = (locale: Locale): boolean => lazyLocales.isLocaleLoaded(locale);

export const loadLocale = (locale: Locale): Promise<void> => lazyLocales.loadLocale(locale);
