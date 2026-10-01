/**
 * @file cardDescriptions.ts
 * @brief Rich card descriptions and titles with glossary hypertext markup.
 */

import * as m from "$lib/paraglide/messages.js";
import { ValueMap } from "$lib/generated/schemas";
import { storeI18n } from "$stores/i18n.svelte";
import type { CardValue } from "$stores/game.svelte";

export interface CardDescriptionInfo {
	title: string;
	description: string;
}

export function getCardDescription(value: CardValue, locale?: string): string {
	const l = locale ?? storeI18n.locale;
	switch (value) {
		case "skip":
			return m.card_desc_skip({}, { locale: l });
		case "reverse":
			return m.card_desc_reverse({}, { locale: l });
		case "+2":
			return m.card_desc_draw2({}, { locale: l });
		case "jolly":
			return m.card_desc_wild({}, { locale: l });
		case "jolly_draw4":
			return m.card_desc_wild_draw4({}, { locale: l });
		case "7":
			return m.card_desc_seven({}, { locale: l });
		case "0":
			return m.card_desc_zero({}, { locale: l });
		default:
			return m.card_desc_number({}, { locale: l });
	}
}

export function getCardTitle(card: { type: string; value: CardValue }, locale?: string): string {
	const l = locale ?? storeI18n.locale;
	switch (card.value) {
		case "skip":
			return m.card_title_skip({}, { locale: l });
		case "reverse":
			return m.card_title_reverse({}, { locale: l });
		case "+2":
			return m.card_title_draw2({}, { locale: l });
		case "jolly":
			return m.card_title_wild({}, { locale: l });
		case "jolly_draw4":
			return m.card_title_wild_draw4({}, { locale: l });
		default: {
			const color = card.type.charAt(0).toUpperCase() + card.type.slice(1);
			return `${color} ${card.value}`;
		}
	}
}

export function getCardInfo(
	card: { type: string; value: CardValue },
	locale?: string
): CardDescriptionInfo {
	return {
		title: getCardTitle(card, locale),
		description: getCardDescription(card.value, locale)
	};
}

const VANILLA_COLORS = ["red", "blue", "green", "yellow"];

const VALUE_BY_SLUG: Record<string, CardValue> = {
	draw2: "+2",
	wild: "jolly",
	wild_draw4: "jolly_draw4"
};

const KNOWN_VALUES = new Set<string>(ValueMap);

function vanillaSlug(localId: string): string {
	for (const color of VANILLA_COLORS) {
		const prefix = `${color}_`;
		if (localId.startsWith(prefix)) return localId.slice(prefix.length);
	}
	return localId;
}

export function cardValueFromKind(kindId: string): CardValue | null {
	const [namespace, localId] = kindId.split(":");
	if (namespace !== "vanilla" || !localId) return null;
	const slug = vanillaSlug(localId);
	const value = VALUE_BY_SLUG[slug] ?? slug;
	return KNOWN_VALUES.has(value) ? (value as CardValue) : null;
}

export function cardInfoByKind(kindId: string, locale?: string): CardDescriptionInfo | null {
	const value = cardValueFromKind(kindId);
	if (!value) return null;
	return {
		title: getCardTitle({ type: "white", value }, locale),
		description: getCardDescription(value, locale)
	};
}
