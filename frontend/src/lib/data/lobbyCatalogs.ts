/**
 * @file lobbyCatalogs.ts
 * @brief Client-side presentation catalogs for the lobby browse screen.
 * Rule ids/labels/descriptions come from the server (storeCatalog); this
 * module only maps ids to client presentation (icons) and holds the future
 * i18n translation overrides. Decks are still mocked, no backend concept.
 */

import type { RuleDefinition } from "$lib/stores/catalog.svelte";

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

/**
 * Translation overrides keyed by rule id. Placeholder for the future i18n
 * system: when a rule id has an entry here it wins over the server label.
 */
export const RULE_TEXT: Record<string, { label: string; description?: string }> = {};

export function ruleLabel(rule: RuleDefinition): string {
	return RULE_TEXT[rule.id]?.label ?? rule.label;
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
