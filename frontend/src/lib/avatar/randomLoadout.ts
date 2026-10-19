import { getCatalogue } from "./catalogue";
import type { AvatarSlot, CatalogueItem, Loadout, PartSlot } from "./types";

const PART_SLOTS: PartSlot[] = ["eyes", "mouth", "head_top", "mask", "bodywear"];

/** 32-bit FNV-1a over the seed text. */
function hashSeed(seed: string): number {
	let hash = 2166136261 >>> 0;
	for (let index = 0; index < seed.length; index++) {
		hash ^= seed.charCodeAt(index);
		hash = Math.imul(hash, 16777619) >>> 0;
	}
	return hash;
}

/** mulberry32: a small seeded stream of numbers in [0, 1). */
function seededRandom(seed: string): () => number {
	let state = hashSeed(seed);
	return () => {
		state = (state + 0x6d2b79f5) >>> 0;
		let mixed = state;
		mixed = Math.imul(mixed ^ (mixed >>> 15), mixed | 1);
		mixed ^= mixed + Math.imul(mixed ^ (mixed >>> 7), mixed | 61);
		return ((mixed ^ (mixed >>> 14)) >>> 0) / 4294967296;
	};
}

function pick<T>(choices: T[], random: () => number): T | undefined {
	if (choices.length === 0) return undefined;
	return choices[Math.floor(random() * choices.length)];
}

function starterItems(slot: AvatarSlot, shapeId: string): CatalogueItem[] {
	return getCatalogue().items.filter(
		(item) =>
			item.slot === slot &&
			item.starter === true &&
			(item.shapes === undefined || item.shapes.includes(shapeId))
	);
}

function starterSwatchIds(): string[] {
	return getCatalogue()
		.palettes.filter((palette) => palette.starter === true)
		.flatMap((palette) => palette.swatches.map((swatch) => swatch.id));
}

/**
 * A random but valid loadout built only from free starter items and starter
 * swatches. INFO: the same seed always gives the same loadout, so a seat look
 * does not change between renders.
 */
export function randomStarterLoadout(seed: string): Loadout {
	const random = seededRandom(seed);
	const swatchIds = starterSwatchIds();

	const shape = pick(starterItems("shape", ""), random);
	const skin = pick(swatchIds, random) ?? "";
	const slots: Loadout["slots"] = {};

	for (const slot of PART_SLOTS) {
		const item = pick(starterItems(slot, shape?.id ?? ""), random);
		if (!item) continue;
		const colours: string[] = [];
		for (let region = 0; region < item.regions; region++) {
			colours.push(pick(swatchIds, random) ?? "");
		}
		slots[slot] = { item: item.id, colours: colours.filter((id) => id !== "") };
	}

	return { shape: shape?.id ?? "", skin, slots };
}

/** The look of one seat of one lobby. */
export function seatLoadout(lobbyId: string, seatIndex: number): Loadout {
	return randomStarterLoadout(`${lobbyId}:${seatIndex}`);
}
