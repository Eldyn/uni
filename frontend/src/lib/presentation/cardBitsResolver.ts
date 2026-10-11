import { isCardBits, type CardResolver } from "./eventBindings";
import { storeCardDefs } from "$lib/stores/cardDefs.svelte";

const MOD_INDEX_SHIFT = 24;
const KIND_INDEX_SHIFT = 12;
const KIND_INDEX_MASK = 0xfff;

/** Resolves compact card bits to a kind id and a face color, through the
 *  loaded defs registry. */
export const resolveCardBits: CardResolver = (bits) => {
	if (!isCardBits(bits)) return undefined;
	const definition = storeCardDefs.lookupByIndex(
		bits >>> MOD_INDEX_SHIFT,
		(bits >>> KIND_INDEX_SHIFT) & KIND_INDEX_MASK
	);
	if (!definition) return undefined;
	return { kind: definition.string_id, color: definition.face.color ?? "white" };
};
