import type { CardType, CardValue } from "$stores/game.svelte";

export interface LoaderFace {
	type: CardType;
	value: CardValue;
}

export const LOADER_CARD_COUNT = 5;

const COLOURED_TYPES: CardType[] = ["red", "blue", "green", "yellow"];
const COLOURED_VALUES: CardValue[] = [
	"0",
	"1",
	"2",
	"3",
	"4",
	"5",
	"6",
	"7",
	"8",
	"9",
	"skip",
	"reverse",
	"+2"
];
const WILD_VALUES: CardValue[] = ["jolly", "jolly_draw4"];

const DECK: LoaderFace[] = [
	...COLOURED_TYPES.flatMap((type) => COLOURED_VALUES.map((value) => ({ type, value }))),
	...WILD_VALUES.map((value) => ({ type: "white" as CardType, value }))
];

function pick<T>(items: readonly T[], random: () => number): T {
	return items[Math.min(items.length - 1, Math.floor(random() * items.length))];
}

export function randomLoaderFace(random: () => number = Math.random): LoaderFace {
	return pick(DECK, random);
}

export function randomLoaderFaces(
	count = LOADER_CARD_COUNT,
	random: () => number = Math.random
): LoaderFace[] {
	return Array.from({ length: count }, () => randomLoaderFace(random));
}
