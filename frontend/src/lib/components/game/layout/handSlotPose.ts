/**
 * @file handSlotPose.ts
 * @brief The single function that owns "hand slot N's full world pose" (X and
 * Y together) — LocalHand3D's own idle render and baseBeats.ts's animation
 * targets both call this instead of each keeping a partial, drifting copy.
 * Keeping one copy fixes a landing card that ignored the Y-stack offset.
 */

import { computeHandLine } from "./handLine";
import { EM_TO_WORLD } from "../three/units";
import type { BoardPlacement } from "./boardPlacement";

// Same z-fighting fix as the other piles/hands: keep the step clear of
// CardMesh3D's own internal layer span (up to 0.004 world units).
export const HAND_STACK_STEP = 0.02;
// How far a dragged card lifts clear of every resting neighbor's own Y-stack
// range, so a drag always wins the raycast/depth test regardless of row size.
export const DRAG_LIFT = 0.5;

export interface HandSlotSnapshot {
	orderIds: number[];
	scrollEm: number;
	maxHalfSpanEm: number;
}

export function handSlotPose(
	index: number,
	cardCount: number,
	snapshot: HandSlotSnapshot,
	placement: BoardPlacement,
	opts: { dragging?: boolean } = {}
): [number, number, number] {
	const handEmToWorld = EM_TO_WORLD * placement.handScale;
	const line = computeHandLine(cardCount, snapshot.maxHalfSpanEm, snapshot.scrollEm);
	const slot = line.slots[index];
	const x = slot ? slot.x * handEmToWorld : 0;
	const y = opts.dragging ? DRAG_LIFT : index * HAND_STACK_STEP;
	return [x, y, placement.localSeatZ];
}
