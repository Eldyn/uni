/**
 * @file avatarTint.ts
 * @brief The sprite tint of a seat avatar. The baked strip carries the
 * player's own colors, so the base tint is white: nothing is multiplied by a
 * seat color. Only the dim (not this seat's turn) and the debt red remain.
 */

import { Color } from "three";

const DIM_FACTOR = 0.45;
const DANGER_COLOR = new Color("#ff4a3a");
const DANGER_MIX = 0.5;

/** The tint of an opponent seat sprite. */
export function opponentAvatarTint(
	dimmed: boolean,
	highlightMix: number,
	isDebtTarget: boolean,
	targetHighlight: Color
): Color {
	return new Color("#ffffff")
		.multiplyScalar(dimmed ? DIM_FACTOR : 1)
		.lerp(targetHighlight, highlightMix)
		.lerp(DANGER_COLOR, isDebtTarget ? DANGER_MIX : 0);
}

/** The tint of the local seat avatar plane. */
export function localAvatarTint(dimmed: boolean, isDebtTarget: boolean): Color {
	if (isDebtTarget) return DANGER_COLOR.clone();
	return new Color("#ffffff").multiplyScalar(dimmed ? DIM_FACTOR : 1);
}
