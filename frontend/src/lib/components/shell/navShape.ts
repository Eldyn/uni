/**
 * @file navShape.ts
 * @brief The single place the shell decides between a bottom nav bar and a
 * left rail. Mirrors the CSS breakpoint `(min-width: 768px), (max-height:
 * 599px)` exactly — the two must never drift apart, since one is what the
 * DOM's ARIA/tab-order logic reads and the other is what the user visually
 * sees. The max-height clause is what a width-only breakpoint gets wrong: a
 * landscape phone (844x390) has plenty of width but no vertical budget for a
 * bottom bar, and needs the rail.
 */

export type NavShape = "bottom" | "rail";

const RAIL_MIN_WIDTH = 768;
const RAIL_MAX_HEIGHT = 599;

/**
 * @brief Decides which nav shape a viewport of the given size should use.
 * @param width Viewport width in CSS pixels.
 * @param height Viewport height in CSS pixels.
 * @returns "rail" if the viewport is wide enough, or short enough that a
 * bottom bar would eat too much of it; "bottom" otherwise.
 */
export function resolveNavShape(width: number, height: number): NavShape {
	if (width >= RAIL_MIN_WIDTH) return "rail";
	if (height <= RAIL_MAX_HEIGHT) return "rail";
	return "bottom";
}
