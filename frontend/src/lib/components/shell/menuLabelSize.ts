/** A tile is 5rem wide with 4 px padding on each side (16 px root). */
export const MENU_TILE_TEXT_WIDTH_PX = 72;
export const MENU_LABEL_MAX_PX = 10.4;
export const MENU_LABEL_MIN_PX = 6;

/** INFO: average advance of a Latin or Cyrillic glyph of the tiny font, in em. */
const NARROW_GLYPH_EM = 0.7;
const WIDE_GLYPH_EM = 1;
/** First code point of the CJK blocks (full-width glyphs). */
const FIRST_WIDE_CODE_POINT = 0x2e80;

function glyphEm(character: string): number {
	const codePoint = character.codePointAt(0) ?? 0;
	return codePoint >= FIRST_WIDE_CODE_POINT ? WIDE_GLYPH_EM : NARROW_GLYPH_EM;
}

/** Width in em of the widest word; the label wraps on spaces only. */
export function widestWordEm(label: string): number {
	const words = label.split(/\s+/).filter((word) => word.length > 0);
	const widths = words.map((word) =>
		[...word].reduce((total, character) => total + glyphEm(character), 0)
	);
	return Math.max(0, ...widths);
}

/** Largest font size (px, one decimal) at which the widest word fits the tile. */
export function menuLabelFontSizePx(label: string): number {
	const widest = widestWordEm(label);
	if (widest === 0) return MENU_LABEL_MAX_PX;
	const fitted = Math.floor((MENU_TILE_TEXT_WIDTH_PX / widest) * 10) / 10;
	return Math.min(MENU_LABEL_MAX_PX, Math.max(MENU_LABEL_MIN_PX, fitted));
}
