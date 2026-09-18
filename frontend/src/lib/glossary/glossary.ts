/**
 * @file glossary.ts
 * @brief Glossary registry for card mechanics, rule keywords, and rich-text hypertext links.
 */

import * as m from "$lib/paraglide/messages.js";
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

export const GLOSSARY_REGISTRY: Record<string, GlossaryDefinition> = {
	draw: {
		id: "draw",
		getTitle: (l) => m.glossary_draw_title({}, { locale: l ?? storeI18n.locale }),
		getDescription: (l) => m.glossary_draw_desc({}, { locale: l ?? storeI18n.locale }),
		getTags: (l) => [{ label: m.glossary_tag_action({}, { locale: l ?? storeI18n.locale }), bg: "#2ca972", shadowColor: "#1d754e" }]
	},
	skip: {
		id: "skip",
		getTitle: (l) => m.glossary_skip_title({}, { locale: l ?? storeI18n.locale }),
		getDescription: (l) => m.glossary_skip_desc({}, { locale: l ?? storeI18n.locale }),
		getTags: (l) => [{ label: m.glossary_tag_action({}, { locale: l ?? storeI18n.locale }), bg: "#2ca972", shadowColor: "#1d754e" }]
	},
	reverse: {
		id: "reverse",
		getTitle: (l) => m.glossary_reverse_title({}, { locale: l ?? storeI18n.locale }),
		getDescription: (l) => m.glossary_reverse_desc({}, { locale: l ?? storeI18n.locale }),
		getTags: (l) => [{ label: m.glossary_tag_action({}, { locale: l ?? storeI18n.locale }), bg: "#2ca972", shadowColor: "#1d754e" }]
	},
	wild: {
		id: "wild",
		getTitle: (l) => m.glossary_wild_title({}, { locale: l ?? storeI18n.locale }),
		getDescription: (l) => m.glossary_wild_desc({}, { locale: l ?? storeI18n.locale }),
		getTags: (l) => [{ label: m.glossary_tag_action({}, { locale: l ?? storeI18n.locale }), bg: "#2ca972", shadowColor: "#1d754e" }]
	},
	turn: {
		id: "turn",
		getTitle: (l) => m.glossary_turn_title({}, { locale: l ?? storeI18n.locale }),
		getDescription: (l) => m.glossary_turn_desc({}, { locale: l ?? storeI18n.locale }),
		getTags: (l) => [{ label: m.glossary_tag_flow({}, { locale: l ?? storeI18n.locale }), bg: "#8b5cf6", shadowColor: "#6d28d9" }]
	},
	color: {
		id: "color",
		getTitle: (l) => m.glossary_color_title({}, { locale: l ?? storeI18n.locale }),
		getDescription: (l) => m.glossary_color_desc({}, { locale: l ?? storeI18n.locale })
	},
	draw_pile: {
		id: "draw_pile",
		getTitle: (l) => m.glossary_draw_pile_title({}, { locale: l ?? storeI18n.locale }),
		getDescription: (l) => m.glossary_draw_pile_desc({}, { locale: l ?? storeI18n.locale }),
		getTags: (l) => [{ label: m.glossary_tag_pile({}, { locale: l ?? storeI18n.locale }), bg: "#d97706", shadowColor: "#b45309" }]
	},
	discard_pile: {
		id: "discard_pile",
		getTitle: (l) => m.glossary_discard_pile_title({}, { locale: l ?? storeI18n.locale }),
		getDescription: (l) => m.glossary_discard_pile_desc({}, { locale: l ?? storeI18n.locale }),
		getTags: (l) => [{ label: m.glossary_tag_pile({}, { locale: l ?? storeI18n.locale }), bg: "#d97706", shadowColor: "#b45309" }]
	},
	play: {
		id: "play",
		getTitle: (l) => m.glossary_play_title({}, { locale: l ?? storeI18n.locale }),
		getDescription: (l) => m.glossary_play_desc({}, { locale: l ?? storeI18n.locale }),
		getTags: (l) => [{ label: m.glossary_tag_action({}, { locale: l ?? storeI18n.locale }), bg: "#2ca972", shadowColor: "#1d754e" }]
	},
	seven_zero: {
		id: "seven_zero",
		getTitle: (l) => m.glossary_seven_zero_title({}, { locale: l ?? storeI18n.locale }),
		getDescription: (l) => m.glossary_seven_zero_desc({}, { locale: l ?? storeI18n.locale }),
		getTags: (l) => [{ label: m.glossary_tag_rule({}, { locale: l ?? storeI18n.locale }), bg: "#2563eb", shadowColor: "#1d4ed8" }]
	},
	draw_stacking: {
		id: "draw_stacking",
		getTitle: (l) => m.glossary_draw_stacking_title({}, { locale: l ?? storeI18n.locale }),
		getDescription: (l) => m.glossary_draw_stacking_desc({}, { locale: l ?? storeI18n.locale }),
		getTags: (l) => [{ label: m.glossary_tag_rule({}, { locale: l ?? storeI18n.locale }), bg: "#2563eb", shadowColor: "#1d4ed8" }]
	},
	force_play: {
		id: "force_play",
		getTitle: (l) => m.glossary_force_play_title({}, { locale: l ?? storeI18n.locale }),
		getDescription: (l) => m.glossary_force_play_desc({}, { locale: l ?? storeI18n.locale }),
		getTags: (l) => [{ label: m.glossary_tag_rule({}, { locale: l ?? storeI18n.locale }), bg: "#2563eb", shadowColor: "#1d4ed8" }]
	},
	jump_in: {
		id: "jump_in",
		getTitle: (l) => m.glossary_jump_in_title({}, { locale: l ?? storeI18n.locale }),
		getDescription: (l) => m.glossary_jump_in_desc({}, { locale: l ?? storeI18n.locale }),
		getTags: (l) => [{ label: m.glossary_tag_rule({}, { locale: l ?? storeI18n.locale }), bg: "#2563eb", shadowColor: "#1d4ed8" }]
	},
	progressive: {
		id: "progressive",
		getTitle: (l) => m.glossary_progressive_title({}, { locale: l ?? storeI18n.locale }),
		getDescription: (l) => m.glossary_progressive_desc({}, { locale: l ?? storeI18n.locale }),
		getTags: (l) => [{ label: m.glossary_tag_rule({}, { locale: l ?? storeI18n.locale }), bg: "#2563eb", shadowColor: "#1d4ed8" }]
	},
	elimination: {
		id: "elimination",
		getTitle: (l) => m.glossary_elimination_title({}, { locale: l ?? storeI18n.locale }),
		getDescription: (l) => m.glossary_elimination_desc({}, { locale: l ?? storeI18n.locale }),
		getTags: (l) => [{ label: m.glossary_tag_rule({}, { locale: l ?? storeI18n.locale }), bg: "#2563eb", shadowColor: "#1d4ed8" }]
	}
};

/**
 * Resolves a keyword definition. Fails soft by returning null for unknown keywords.
 */
export function getGlossaryEntry(keyword: string, locale?: string): GlossaryEntry | null {
	const def = GLOSSARY_REGISTRY[keyword];
	if (!def) return null;
	return {
		id: def.id,
		title: def.getTitle(locale),
		description: def.getDescription(locale),
		tags: def.getTags ? def.getTags(locale) : undefined
	};
}

export function hasGlossaryKeyword(keyword: string): boolean {
	return Boolean(GLOSSARY_REGISTRY[keyword]);
}
