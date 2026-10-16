/**
 * @file logGoldenSupport.ts
 * @brief Test support for the play log text golden: where the template of a
 * legacy log key lives, and how a legacy key maps to the key the code uses.
 */

import { SHIPPED_LOCALES } from "../glossary/coreTextsHarness";

const LEGACY_PREFIX = "log_";
const MOD_PREFIX = "uni:log.";

const textsFiles = import.meta.glob<Record<string, string>>(
	"../../../../../mods/uni/texts/*.json",
	{ eager: true, import: "default" }
);

function textsOf(locale: string): Record<string, string> {
	const path = Object.keys(textsFiles).find((file) => file.endsWith(`/${locale}.json`));
	if (path === undefined) throw new Error(`missing texts ${locale}`);
	return textsFiles[path];
}

/** The key under which the code resolves the text of a legacy key. */
export function codeKeyOf(legacyKey: string): string {
	return `${MOD_PREFIX}${legacyKey.slice(LEGACY_PREFIX.length)}`;
}

/** The legacy key of a key that the code uses. */
export function legacyKeyOf(codeKey: string): string {
	return `${LEGACY_PREFIX}${codeKey.slice(MOD_PREFIX.length)}`;
}

/** The unresolved template of a legacy key in the `uni` texts of a locale, or undefined when missing. */
export function templateOf(locale: string, legacyKey: string): string | undefined {
	return textsOf(locale)[codeKeyOf(legacyKey)];
}

export { SHIPPED_LOCALES };
