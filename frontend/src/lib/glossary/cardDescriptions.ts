/**
 * @file cardDescriptions.ts
 * @brief Rich card descriptions and titles with glossary hypertext markup.
 *
 * The texts of the base cards live in the `uni` mod (`<card>.label` and
 * `<card>.description`) and come through the mod text layer. They follow the
 * active locale. A reader gets null while the texts load.
 */

import { isBaseModNamespace } from "$lib/utils/baseMod";
import { ValueMap } from "$lib/generated/schemas";
import { storeModGlossary } from "$stores/modGlossary.svelte";
import { storeModTexts } from "$stores/modTexts.svelte";
import type { CardValue } from "$stores/game.svelte";

export interface CardDescriptionInfo {
	title: string;
	description: string;
}

const BASE_COLORS = ["red", "blue", "green", "yellow"];
const REPRESENTATIVE_COLOR = "red";
const BASE_KIND_PREFIX = "uni:";
const LABEL_SUFFIX = ".label";
const DESCRIPTION_SUFFIX = ".description";

const SLUG_BY_VALUE: Partial<Record<CardValue, string>> = {
	skip: "skip",
	reverse: "reverse",
	"+2": "draw2",
	jolly: "wild",
	jolly_draw4: "wild_draw4"
};

const VALUE_BY_SLUG: Record<string, CardValue> = {
	draw2: "+2",
	wild: "jolly",
	wild_draw4: "jolly_draw4"
};

const COLORLESS_SLUGS = new Set(["wild", "wild_draw4"]);

/** The base kind that carries the texts of a card value; the texts do not depend on the colour. */
function representativeKind(value: CardValue): string {
	const slug = SLUG_BY_VALUE[value] ?? String(value);
	return COLORLESS_SLUGS.has(slug)
		? `${BASE_KIND_PREFIX}${slug}`
		: `${BASE_KIND_PREFIX}${REPRESENTATIVE_COLOR}_${slug}`;
}

/**
 * The text of a card kind, or null while the `defs` listing or the texts are
 * missing. A kind that no listing names never shows its raw key.
 */
function resolveCardText(kindId: string, suffix: string): string | null {
	if (!storeModGlossary.has(kindId)) return null;
	return storeModTexts.resolve(`${kindId}${suffix}`, {});
}

export function getCardDescription(value: CardValue): string | null {
	return resolveCardText(representativeKind(value), DESCRIPTION_SUFFIX);
}

export function getCardTitle(card: { type: string; value: CardValue }): string | null {
	if (SLUG_BY_VALUE[card.value] === undefined) {
		const color = card.type.charAt(0).toUpperCase() + card.type.slice(1);
		return `${color} ${card.value}`;
	}
	return resolveCardText(representativeKind(card.value), LABEL_SUFFIX);
}

export function getCardInfo(card: { type: string; value: CardValue }): CardDescriptionInfo | null {
	const title = getCardTitle(card);
	const description = getCardDescription(card.value);
	return title === null || description === null ? null : { title, description };
}

function baseSlug(localId: string): string {
	for (const color of BASE_COLORS) {
		const prefix = `${color}_`;
		if (localId.startsWith(prefix)) return localId.slice(prefix.length);
	}
	return localId;
}

const KNOWN_VALUES = new Set<string>(ValueMap);

export function cardValueFromKind(kindId: string): CardValue | null {
	const [namespace, localId] = kindId.split(":");
	if (!isBaseModNamespace(namespace) || !localId) return null;
	const slug = baseSlug(localId);
	const value = VALUE_BY_SLUG[slug] ?? slug;
	return KNOWN_VALUES.has(value) ? (value as CardValue) : null;
}

/**
 * The colour-free title and the description of a base card kind. A title
 * without a colour word suits copy that already conveys the colour separately:
 * specials keep their localized title, numbers are just the value.
 */
export function cardInfoByKind(kindId: string): CardDescriptionInfo | null {
	const value = cardValueFromKind(kindId);
	if (!value) return null;
	const title =
		SLUG_BY_VALUE[value] === undefined ? String(value) : resolveCardText(kindId, LABEL_SUFFIX);
	const description = resolveCardText(kindId, DESCRIPTION_SUFFIX);
	return title === null || description === null ? null : { title, description };
}
