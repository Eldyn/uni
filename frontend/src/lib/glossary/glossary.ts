/**
 * @file glossary.ts
 * @brief Glossary registry for card mechanics, rule keywords, and rich-text hypertext links.
 */

import * as m from "$lib/paraglide/messages.js";
import { storeI18n } from "$stores/i18n.svelte";

export interface GlossaryEntry {
	id: string;
	title: string;
	description: string;
}

export interface GlossaryDefinition {
	id: string;
	getTitle: (locale?: string) => string;
	getDescription: (locale?: string) => string;
}

export const GLOSSARY_REGISTRY: Record<string, GlossaryDefinition> = {
	draw: {
		id: "draw",
		getTitle: (l) => m.glossary_draw_title({}, { locale: l ?? storeI18n.locale }),
		getDescription: (l) => m.glossary_draw_desc({}, { locale: l ?? storeI18n.locale })
	},
	skip: {
		id: "skip",
		getTitle: (l) => m.glossary_skip_title({}, { locale: l ?? storeI18n.locale }),
		getDescription: (l) => m.glossary_skip_desc({}, { locale: l ?? storeI18n.locale })
	},
	reverse: {
		id: "reverse",
		getTitle: (l) => m.glossary_reverse_title({}, { locale: l ?? storeI18n.locale }),
		getDescription: (l) => m.glossary_reverse_desc({}, { locale: l ?? storeI18n.locale })
	},
	wild: {
		id: "wild",
		getTitle: (l) => m.glossary_wild_title({}, { locale: l ?? storeI18n.locale }),
		getDescription: (l) => m.glossary_wild_desc({}, { locale: l ?? storeI18n.locale })
	},
	turn: {
		id: "turn",
		getTitle: (l) => m.glossary_turn_title({}, { locale: l ?? storeI18n.locale }),
		getDescription: (l) => m.glossary_turn_desc({}, { locale: l ?? storeI18n.locale })
	},
	color: {
		id: "color",
		getTitle: (l) => m.glossary_color_title({}, { locale: l ?? storeI18n.locale }),
		getDescription: (l) => m.glossary_color_desc({}, { locale: l ?? storeI18n.locale })
	},
	draw_pile: {
		id: "draw_pile",
		getTitle: (l) => m.glossary_draw_pile_title({}, { locale: l ?? storeI18n.locale }),
		getDescription: (l) => m.glossary_draw_pile_desc({}, { locale: l ?? storeI18n.locale })
	},
	discard_pile: {
		id: "discard_pile",
		getTitle: (l) => m.glossary_discard_pile_title({}, { locale: l ?? storeI18n.locale }),
		getDescription: (l) => m.glossary_discard_pile_desc({}, { locale: l ?? storeI18n.locale })
	},
	play: {
		id: "play",
		getTitle: (l) => m.glossary_play_title({}, { locale: l ?? storeI18n.locale }),
		getDescription: (l) => m.glossary_play_desc({}, { locale: l ?? storeI18n.locale })
	},
	seven_zero: {
		id: "seven_zero",
		getTitle: (l) => m.glossary_seven_zero_title({}, { locale: l ?? storeI18n.locale }),
		getDescription: (l) => m.glossary_seven_zero_desc({}, { locale: l ?? storeI18n.locale })
	},
	draw_stacking: {
		id: "draw_stacking",
		getTitle: (l) => m.glossary_draw_stacking_title({}, { locale: l ?? storeI18n.locale }),
		getDescription: (l) => m.glossary_draw_stacking_desc({}, { locale: l ?? storeI18n.locale })
	},
	force_play: {
		id: "force_play",
		getTitle: (l) => m.glossary_force_play_title({}, { locale: l ?? storeI18n.locale }),
		getDescription: (l) => m.glossary_force_play_desc({}, { locale: l ?? storeI18n.locale })
	},
	jump_in: {
		id: "jump_in",
		getTitle: (l) => m.glossary_jump_in_title({}, { locale: l ?? storeI18n.locale }),
		getDescription: (l) => m.glossary_jump_in_desc({}, { locale: l ?? storeI18n.locale })
	},
	progressive: {
		id: "progressive",
		getTitle: (l) => m.glossary_progressive_title({}, { locale: l ?? storeI18n.locale }),
		getDescription: (l) => m.glossary_progressive_desc({}, { locale: l ?? storeI18n.locale })
	},
	elimination: {
		id: "elimination",
		getTitle: (l) => m.glossary_elimination_title({}, { locale: l ?? storeI18n.locale }),
		getDescription: (l) => m.glossary_elimination_desc({}, { locale: l ?? storeI18n.locale })
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
		description: def.getDescription(locale)
	};
}

export function hasGlossaryKeyword(keyword: string): boolean {
	return Boolean(GLOSSARY_REGISTRY[keyword]);
}
