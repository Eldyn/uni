import { getCatalogue } from "./catalogue";
import type { CatalogueItem } from "./types";

/**
 * Where the item's art sits inside one frame for the given shape.
 * A `strip` already bobs in place, so it needs no offset. A `still` sits on
 * its socket plus the per-frame bob. A missing socket or shape means no shift.
 */
export function frameOffset(shapeId: string, item: CatalogueItem, frame: number): [number, number] {
	if (item.asset.kind === "strip") return [0, 0];
	if (!item.socket) return [0, 0];

	const socket = getCatalogue().shape_sockets[shapeId]?.[item.socket];
	if (!socket) return [0, 0];

	const bob = item.asset.bob[frame] ?? [0, 0];
	return [socket[0] + bob[0], socket[1] + bob[1]];
}
