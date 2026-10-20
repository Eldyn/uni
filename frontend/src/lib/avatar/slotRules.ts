import { getSwatch, resolveItem } from "./catalogue";
import { hexToRgb, type Rgb } from "./tint";
import type { AvatarSlot, CatalogueItem, Loadout, PartSlot, SlotChoice } from "./types";

/** Bumped whenever the art or the layer rules change the baked pixels. */
export const ART_VERSION = 3;

export interface ResolvedLayer {
	slot: AvatarSlot;
	item: CatalogueItem;
	colours: Rgb[];
}

const PART_ORDER: PartSlot[] = ["bodywear", "mouth", "eyes", "mask", "head_top"];

/**
 * The layers a loadout draws, back to front: the shape and the motif (region 0
 * follows the skin), then the parts. A worn set is the full player: it replaces
 * the shape, the motif and every part slot. An unknown item falls back to its
 * slot default; a slot with no default is dropped. Region colours are RGB; a
 * region with no colour is left as a gap.
 */
export function resolveLayers(loadout: Loadout): ResolvedLayer[] {
	const layers: ResolvedLayer[] = [];

	const setChoice = loadout.slots.set;
	if (setChoice) {
		const set = resolveItem(setChoice.item, "set");
		if (set) {
			return [{ slot: "set", item: set, colours: slotColours(set, setChoice) }];
		}
	}

	const skin = swatchColour(loadout.skin);

	const shape = resolveItem(loadout.shape, "shape");
	if (shape) {
		const colours: Rgb[] = [];
		if (skin) colours[0] = skin;
		layers.push({ slot: "shape", item: shape, colours });
	}

	const motifChoice = loadout.slots.motif;
	if (motifChoice) {
		const motif = resolveItem(motifChoice.item, "motif");
		if (motif) {
			layers.push({ slot: "motif", item: motif, colours: motifColours(motif, motifChoice, skin) });
		}
	}

	for (const slot of PART_ORDER) {
		const choice = loadout.slots[slot];
		if (!choice) continue;
		const item = resolveItem(choice.item, slot);
		if (!item) continue;
		layers.push({ slot, item, colours: slotColours(item, choice) });
	}

	return layers;
}

/** A stable hash of what the loadout renders, for the bake cache key. */
export function loadoutHash(loadout: Loadout): string {
	return hashWithVersion(loadout, ART_VERSION);
}

/** `loadoutHash` with an explicit art version, so the version can be tested. */
export function hashWithVersion(loadout: Loadout, version: number): string {
	const layers = resolveLayers(loadout).map((layer) => ({
		slot: layer.slot,
		item: layer.item.id,
		masks: layer.item.asset.masks ?? [],
		tint: layer.item.tint ?? "mask",
		colours: layer.colours.map((colour) => (colour ? [...colour] : null))
	}));
	return fnv1aHex(JSON.stringify({ version, layers }));
}

/** Region colors for a part: the chosen swatch, else the item default, per region. */
function slotColours(item: CatalogueItem, choice: SlotChoice): Rgb[] {
	const colours: Rgb[] = [];
	for (let index = 0; index < item.regions; index++) {
		const colour = chosenOrDefault(item, choice, index);
		if (colour) colours[index] = colour;
	}
	return colours;
}

/** Motif colors: region 0 follows the skin, the rest take the choice or default. */
function motifColours(item: CatalogueItem, choice: SlotChoice, skin: Rgb | undefined): Rgb[] {
	const colours: Rgb[] = [];
	if (item.regions > 0 && skin) colours[0] = skin;

	for (let index = 0; index < Math.max(0, item.regions - 1); index++) {
		const colour = chosenOrDefault(item, choice, index);
		if (colour) colours[index + 1] = colour;
	}
	return colours;
}

/** The chosen swatch at a stored index, else the item's default entry there. */
function chosenOrDefault(item: CatalogueItem, choice: SlotChoice, index: number): Rgb | undefined {
	return swatchColour(choice.colours[index]) ?? defaultColour(item.default_colors?.[index]);
}

/** A default entry is a "#rrggbb" hex or a palette swatch id. */
function defaultColour(entry: string | undefined): Rgb | undefined {
	if (!entry) return undefined;
	return entry.startsWith("#") ? hexToRgb(entry) : swatchColour(entry);
}

function swatchColour(swatchId: string | undefined): Rgb | undefined {
	const swatch = swatchId ? getSwatch(swatchId) : undefined;
	return swatch ? hexToRgb(swatch.hex) : undefined;
}

/** 32-bit FNV-1a over the input, as eight hex digits. */
function fnv1aHex(input: string): string {
	let hash = 2166136261 >>> 0;
	for (let index = 0; index < input.length; index++) {
		hash ^= input.charCodeAt(index);
		hash = Math.imul(hash, 16777619) >>> 0;
	}
	return hash.toString(16).padStart(8, "0");
}
