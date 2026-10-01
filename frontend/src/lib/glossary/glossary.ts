/**
 * @file glossary.ts
 * @brief Glossary registry for card mechanics, rule keywords, and rich-text hypertext links.
 */

import * as m from "$lib/paraglide/messages.js";
import { cardInfoByKind } from "$lib/glossary/cardDescriptions";
import { storeI18n } from "$stores/i18n.svelte";

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

export interface GlossaryDefinition {
	id: string;
	getTitle: (locale?: string) => string;
	getDescription: (locale?: string) => string;
	getTags?: (locale?: string) => GlossaryTag[];
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

export const GLOSSARY_REGISTRY: Record<string, GlossaryDefinition> = {
	"vanilla:draw": {
		id: "vanilla:draw",
		getTitle: (l) => m.glossary_draw_title({}, { locale: l ?? storeI18n.locale }),
		getDescription: (l) => m.glossary_draw_desc({}, { locale: l ?? storeI18n.locale }),
		getTags: (l) => [
			glossaryTag("action", m.glossary_tag_action({}, { locale: l ?? storeI18n.locale }))
		]
	},
	"vanilla:skip": {
		id: "vanilla:skip",
		getTitle: (l) => m.glossary_skip_title({}, { locale: l ?? storeI18n.locale }),
		getDescription: (l) => m.glossary_skip_desc({}, { locale: l ?? storeI18n.locale }),
		getTags: (l) => [
			glossaryTag("action", m.glossary_tag_action({}, { locale: l ?? storeI18n.locale }))
		]
	},
	"vanilla:reverse": {
		id: "vanilla:reverse",
		getTitle: (l) => m.glossary_reverse_title({}, { locale: l ?? storeI18n.locale }),
		getDescription: (l) => m.glossary_reverse_desc({}, { locale: l ?? storeI18n.locale }),
		getTags: (l) => [
			glossaryTag("action", m.glossary_tag_action({}, { locale: l ?? storeI18n.locale }))
		]
	},
	"vanilla:wild": {
		id: "vanilla:wild",
		getTitle: (l) => m.glossary_wild_title({}, { locale: l ?? storeI18n.locale }),
		getDescription: (l) => m.glossary_wild_desc({}, { locale: l ?? storeI18n.locale }),
		getTags: (l) => [
			glossaryTag("action", m.glossary_tag_action({}, { locale: l ?? storeI18n.locale }))
		]
	},
	"vanilla:turn": {
		id: "vanilla:turn",
		getTitle: (l) => m.glossary_turn_title({}, { locale: l ?? storeI18n.locale }),
		getDescription: (l) => m.glossary_turn_desc({}, { locale: l ?? storeI18n.locale }),
		getTags: (l) => [
			glossaryTag("flow", m.glossary_tag_flow({}, { locale: l ?? storeI18n.locale }))
		]
	},
	"vanilla:color": {
		id: "vanilla:color",
		getTitle: (l) => m.glossary_color_title({}, { locale: l ?? storeI18n.locale }),
		getDescription: (l) => m.glossary_color_desc({}, { locale: l ?? storeI18n.locale })
	},
	"vanilla:draw_pile": {
		id: "vanilla:draw_pile",
		getTitle: (l) => m.glossary_draw_pile_title({}, { locale: l ?? storeI18n.locale }),
		getDescription: (l) => m.glossary_draw_pile_desc({}, { locale: l ?? storeI18n.locale }),
		getTags: (l) => [
			glossaryTag("pile", m.glossary_tag_pile({}, { locale: l ?? storeI18n.locale }))
		]
	},
	"vanilla:discard_pile": {
		id: "vanilla:discard_pile",
		getTitle: (l) => m.glossary_discard_pile_title({}, { locale: l ?? storeI18n.locale }),
		getDescription: (l) => m.glossary_discard_pile_desc({}, { locale: l ?? storeI18n.locale }),
		getTags: (l) => [
			glossaryTag("pile", m.glossary_tag_pile({}, { locale: l ?? storeI18n.locale }))
		]
	},
	"vanilla:play": {
		id: "vanilla:play",
		getTitle: (l) => m.glossary_play_title({}, { locale: l ?? storeI18n.locale }),
		getDescription: (l) => m.glossary_play_desc({}, { locale: l ?? storeI18n.locale }),
		getTags: (l) => [
			glossaryTag("action", m.glossary_tag_action({}, { locale: l ?? storeI18n.locale }))
		]
	},
	"vanilla:seven_zero": {
		id: "vanilla:seven_zero",
		getTitle: (l) => m.glossary_seven_zero_title({}, { locale: l ?? storeI18n.locale }),
		getDescription: (l) => m.glossary_seven_zero_desc({}, { locale: l ?? storeI18n.locale }),
		getTags: (l) => [
			glossaryTag("rule", m.glossary_tag_rule({}, { locale: l ?? storeI18n.locale }))
		]
	},
	"vanilla:draw_stacking": {
		id: "vanilla:draw_stacking",
		getTitle: (l) => m.glossary_draw_stacking_title({}, { locale: l ?? storeI18n.locale }),
		getDescription: (l) => m.glossary_draw_stacking_desc({}, { locale: l ?? storeI18n.locale }),
		getTags: (l) => [
			glossaryTag("rule", m.glossary_tag_rule({}, { locale: l ?? storeI18n.locale }))
		]
	},
	"vanilla:force_play": {
		id: "vanilla:force_play",
		getTitle: (l) => m.glossary_force_play_title({}, { locale: l ?? storeI18n.locale }),
		getDescription: (l) => m.glossary_force_play_desc({}, { locale: l ?? storeI18n.locale }),
		getTags: (l) => [
			glossaryTag("rule", m.glossary_tag_rule({}, { locale: l ?? storeI18n.locale }))
		]
	},
	"vanilla:jump_in": {
		id: "vanilla:jump_in",
		getTitle: (l) => m.glossary_jump_in_title({}, { locale: l ?? storeI18n.locale }),
		getDescription: (l) => m.glossary_jump_in_desc({}, { locale: l ?? storeI18n.locale }),
		getTags: (l) => [
			glossaryTag("rule", m.glossary_tag_rule({}, { locale: l ?? storeI18n.locale }))
		]
	},
	"vanilla:progressive": {
		id: "vanilla:progressive",
		getTitle: (l) => m.glossary_progressive_title({}, { locale: l ?? storeI18n.locale }),
		getDescription: (l) => m.glossary_progressive_desc({}, { locale: l ?? storeI18n.locale }),
		getTags: (l) => [
			glossaryTag("rule", m.glossary_tag_rule({}, { locale: l ?? storeI18n.locale }))
		]
	},
	"vanilla:elimination": {
		id: "vanilla:elimination",
		getTitle: (l) => m.glossary_elimination_title({}, { locale: l ?? storeI18n.locale }),
		getDescription: (l) => m.glossary_elimination_desc({}, { locale: l ?? storeI18n.locale }),
		getTags: (l) => [
			glossaryTag("rule", m.glossary_tag_rule({}, { locale: l ?? storeI18n.locale }))
		]
	}
};

/**
 * Resolves a keyword definition. Fails soft by returning null for unknown keywords.
 */
export function getGlossaryEntry(keyword: string, locale?: string): GlossaryEntry | null {
	const def = GLOSSARY_REGISTRY[keyword];
	if (def) {
		return {
			id: def.id,
			title: def.getTitle(locale),
			description: def.getDescription(locale),
			tags: def.getTags ? def.getTags(locale) : undefined
		};
	}
	const card = cardInfoByKind(keyword, locale);
	if (!card) return null;
	return { id: keyword, title: card.title, description: card.description };
}

export function hasGlossaryKeyword(keyword: string): boolean {
	return getGlossaryEntry(keyword) !== null;
}
