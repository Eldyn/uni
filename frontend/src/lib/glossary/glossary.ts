/**
 * @file glossary.ts
 * @brief Glossary registry for card mechanics, rule keywords, and rich-text hypertext links.
 */

import * as m from "$lib/paraglide/messages.js";
import { storeI18n } from "$stores/i18n.svelte";
import { storeModGlossary } from "$stores/modGlossary.svelte";

export interface GlossaryTag {
	label: string;
	bg: string;
	shadowColor?: string;
	textColor?: string;
}

export interface GlossaryEntry {
	id: string;
	title: string;
	description: string;
	tags?: GlossaryTag[];
}

export type GlossaryTagKind = "action" | "flow" | "rule" | "pile";

const GLOSSARY_TAG_PRESETS: Record<GlossaryTagKind, { bg: string; shadowColor: string }> = {
	action: { bg: "var(--glossary-tag-action)", shadowColor: "var(--glossary-tag-action-shadow)" },
	flow: { bg: "var(--glossary-tag-flow)", shadowColor: "var(--glossary-tag-flow-shadow)" },
	rule: { bg: "var(--glossary-tag-rule)", shadowColor: "var(--glossary-tag-rule-shadow)" },
	pile: { bg: "var(--glossary-tag-pile)", shadowColor: "var(--glossary-tag-pile-shadow)" }
};

/**
 * Builds a glossary tag from the shared, token-backed preset for its kind.
 * Single source of truth so tooltip pills, the rules grid, and any future
 * glossary surface cannot drift apart (the colours live in app.css).
 */
export function glossaryTag(kind: GlossaryTagKind, label: string): GlossaryTag {
	const preset = GLOSSARY_TAG_PRESETS[kind];
	return { label, bg: preset.bg, shadowColor: preset.shadowColor };
}

/**
 * The pill kind of each core mechanic. The texts of a core entry come from the
 * `uni` mod; only the pill, a look of the interface, stays here. The labels of
 * the pills belong to the interface catalog.
 */
const UNI_TAG_KINDS: Record<string, GlossaryTagKind> = {
	"uni:draw": "action",
	"uni:skip": "action",
	"uni:reverse": "action",
	"uni:wild": "action",
	"uni:play": "action",
	"uni:turn": "flow",
	"uni:draw_pile": "pile",
	"uni:discard_pile": "pile",
	"uni:seven_zero": "rule",
	"uni:draw_stacking": "rule",
	"uni:force_play": "rule",
	"uni:jump_in": "rule",
	"uni:progressive": "rule",
	"uni:race": "rule"
};

function tagLabel(kind: GlossaryTagKind, locale: string): string {
	switch (kind) {
		case "action":
			return m.glossary_tag_action({}, { locale });
		case "flow":
			return m.glossary_tag_flow({}, { locale });
		case "rule":
			return m.glossary_tag_rule({}, { locale });
		case "pile":
			return m.glossary_tag_pile({}, { locale });
	}
}

/**
 * Resolves a keyword definition. Fails soft by returning null for unknown
 * keywords and while the texts of the entry load. Every entry, the core ones
 * of the `uni` mod too, comes from the glossary listing of the `defs` packet,
 * and its texts from the mod text layer. The texts follow the active locale;
 * `locale` only picks the language of the pill label.
 */
export function getGlossaryEntry(keyword: string, locale?: string): GlossaryEntry | null {
	const entry = storeModGlossary.entry(keyword);
	if (entry === null) return null;
	const tagKind = UNI_TAG_KINDS[keyword];
	return {
		...entry,
		tags: tagKind
			? [glossaryTag(tagKind, tagLabel(tagKind, locale ?? storeI18n.locale))]
			: undefined
	};
}

export function hasGlossaryKeyword(keyword: string): boolean {
	return getGlossaryEntry(keyword) !== null;
}
