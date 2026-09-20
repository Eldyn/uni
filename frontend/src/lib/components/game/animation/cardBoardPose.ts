/**
 * @file cardBoardPose.ts
 * @brief Applies the spectator spin's board yaw to a card that belongs to the
 * table (a ring card, a discard-pile card, a draw-pile card).
 *
 * Card meshes are mounted by AllCards3D at absolute world coordinates rather
 * than under Scene3D's rotating `boardGroup` — that is what lets the local hand
 * row stay upright while the table turns under it, since the hand's cards are
 * the same meshes. The cost is that a table-bound card's owner computes its
 * pose as if the board were at yaw 0, and this module folds the live yaw in at
 * render time.
 *
 * Keeping the whole fold in one pure function means AllCards3D (which renders
 * the card) and baseBeats (whose flight anchors must land on the same rotated
 * pile) can never disagree about where a spinning board puts a card.
 */

import { rotateBoardPoint, rotateBoardSpinDeg } from "../layout/boardRotation";

export interface CardBoardPose {
	x: number;
	y: number;
	z: number;
	spinDeg: number;
}

/** A pose plus the flag saying whether the board yaw applies to it. Hand cards
 *  (`tableBound: false`) are the viewer's own UI and never rotate. */
export interface BoardBoundPose extends CardBoardPose {
	tableBound?: boolean;
}

/**
 * Rotates a card's world pose by the board yaw iff the card is table-bound.
 * Returns a new object; the input pose is never mutated (it is the CardRegistry
 * entry's own reactive `$state`, which must keep holding the canonical,
 * unrotated values so repeated renders don't compound the rotation).
 */
export function poseWithBoardRotation(pose: BoardBoundPose, yaw: number): CardBoardPose {
	if (!pose.tableBound || yaw === 0) {
		return { x: pose.x, y: pose.y, z: pose.z, spinDeg: pose.spinDeg };
	}
	const { x, z } = rotateBoardPoint({ x: pose.x, z: pose.z }, yaw);
	return {
		x,
		y: pose.y,
		z,
		spinDeg: rotateBoardSpinDeg(pose.spinDeg, yaw)
	};
}

/**
 * Rotates a plain flight anchor (an [x, y, z] world position) by the board yaw.
 * Used by baseBeats for anchors that are unambiguously table-bound — the
 * discard pile, the draw pile, and opponent seats.
 */
export function anchorWithBoardRotation(
	anchor: readonly [number, number, number],
	yaw: number
): [number, number, number] {
	if (yaw === 0) return [anchor[0], anchor[1], anchor[2]];
	const { x, z } = rotateBoardPoint({ x: anchor[0], z: anchor[2] }, yaw);
	return [x, anchor[1], z];
}
