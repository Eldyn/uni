/**
 * @file homePalette.ts
 * @brief The two colors of the home screen background, shared with the Shop
 * and Wardrobe backdrop so both screens use the exact same palette.
 */

/** Flat field color. Equals `--bg` in app.css. */
export const HOME_BACKGROUND_BASE_HEX = 0x16171d;
/** The lighter ripple and band color. */
export const HOME_BACKGROUND_HINT_HEX = 0x2c354d;

/** Raw 0..1 components. Not color managed on purpose (see RippleQuad). */
export function hexToRgb01(hex: number): [number, number, number] {
	return [((hex >> 16) & 255) / 255, ((hex >> 8) & 255) / 255, (hex & 255) / 255];
}

export function hexToCss(hex: number): string {
	return `#${hex.toString(16).padStart(6, "0")}`;
}

/** The flat backdrop field. Also the color under the canvas and its fallback. */
export const HOME_BACKGROUND_HINT_CSS = hexToCss(HOME_BACKGROUND_HINT_HEX);
