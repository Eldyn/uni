/**
 * Scene render-order tiers (Three.js `renderOrder`), bottom to top. These are
 * the only numbers that decide in-scene layering, so every participant states
 * its tier explicitly instead of relying on Three.js's default depth/insertion
 * sort (which is what produced dragged-card z-fighting against the discard
 * pile and opponent seats).
 *
 *   0   unclassed scene objects (playmat, piles, vignette-adjacent meshes)
 *   5   idle cards — local hand, opponent ring, discard pile (AllCards3D default)
 *  10   lifted/hovered card (`liftT`)
 *  12   opponent seat avatar + name-label sprites (below any card in the hand)
 *  20   the local card drawn pending a play/draw decision
 *  30   a card being dragged (`dragT > 0`) — the top in-scene tier
 *
 * The dragged tier is deliberately the highest number in the scene: a dragged
 * card must render above the discard pile and every opponent seat. It still
 * cannot escape the `<canvas>`: the HUD/topbar DOM layer lives in a separate
 * stacking context above the canvas (GameScreen's `.game-controls` z-index 2
 * over `.game-board-container` z-index 1), so it always wins regardless of
 * anything set here.
 */
export const RENDER_ORDER = {
	idle: 5,
	lifted: 10,
	seatSprite: 12,
	pendingPlay: 20,
	dragged: 30
} as const;

/** Resolve a card's tier from its pose. Dragging outranks the pending-play
 *  card, which outranks a hovered/lifted card, which outranks an idle one. */
export function cardRenderOrder(
	pose: { dragT?: number; liftT?: number },
	isPendingPlayDrawn: boolean
): number {
	if ((pose.dragT ?? 0) > 0) return RENDER_ORDER.dragged;
	if (isPendingPlayDrawn) return RENDER_ORDER.pendingPlay;
	if (pose.liftT) return RENDER_ORDER.lifted;
	return RENDER_ORDER.idle;
}
