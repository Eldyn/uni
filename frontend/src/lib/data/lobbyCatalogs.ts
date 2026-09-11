/**
 * @file lobbyCatalogs.ts
 * @brief Client-side presentation catalogs for the lobby browse screen.
 * Rule ids/labels/descriptions come from the server (storeCatalog); this
 * module only maps ids to client presentation (icons) and holds the future
 * i18n translation overrides. Decks are still mocked, no backend concept.
 */

import type { RuleDefinition } from "$lib/stores/catalog.svelte";
import { locales } from "$lib/paraglide/runtime.js";
import * as m from "$lib/paraglide/messages.js";

type Locale = (typeof locales)[number];

/** Icon class per rule id (backend RuleRegistrar names). Unknown ids fall
 *  back to DEFAULT_RULE_ICON. */
export const RULE_ICONS: Record<string, string> = {
	draw_stacking: "pixelart-icons-font-grid-3x3",
	seven_zero: "pixelart-icons-font-shuffle",
	jump_in: "pixelart-icons-font-login",
	force_play: "pixelart-icons-font-download",
	no_bluffing: "pixelart-icons-font-skull",
	progressive: "pixelart-icons-font-zap"
};

export const DEFAULT_RULE_ICON = "pixelart-icons-font-star";

export function ruleIcon(id: string): string {
	return RULE_ICONS[id] ?? DEFAULT_RULE_ICON;
}

/** i18n label per known rule id; unknown ids fall back to the server's label. */
const RULE_LABELS: Record<string, (locale: Locale) => string> = {
	draw_stacking: (locale) => m.rule_label_draw_stacking({}, { locale }),
	seven_zero: (locale) => m.rule_label_seven_zero({}, { locale }),
	jump_in: (locale) => m.rule_label_jump_in({}, { locale }),
	force_play: (locale) => m.rule_label_force_play({}, { locale }),
	no_bluffing: (locale) => m.rule_label_no_bluffing({}, { locale }),
	progressive: (locale) => m.rule_label_progressive({}, { locale })
};

export function ruleLabel(rule: RuleDefinition, locale: Locale): string {
	return RULE_LABELS[rule.id]?.(locale) ?? rule.label;
}

/** TODO: mocked, the backend has no deck concept yet. */
export const DECKS = ["Default", "Classic", "Speed", "Chaos", "Starter"];

export const AVATAR_COLORS = [
	"#0493de",
	"#018d41",
	"#dc251c",
	"#fcf604",
	"#c084fc",
	"#ff9f43",
	"#00d2d3",
	"#ee5253"
];

export type SortKey = "fullest" | "emptiest";

/** `filled` drives the 4-slot player preview shown in the sort control.
 *  Display labels are i18n'd at the point of use (see BrowseToolbar's
 *  SORT_LABELS), not stored here. */
export const SORT_OPTIONS: { value: SortKey; filled: number }[] = [
	{ value: "fullest", filled: 3 },
	{ value: "emptiest", filled: 1 }
];
