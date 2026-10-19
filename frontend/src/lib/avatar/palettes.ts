/**
 * @file palettes.ts
 * @brief Palette lookups shared by the Wardrobe picker and the Shop.
 */
import type { Palette } from "./types";

/** The palette that holds the swatch, or undefined. */
export function paletteOfSwatch(
	palettes: readonly Palette[],
	swatchId: string | undefined
): Palette | undefined {
	if (!swatchId) return undefined;
	return palettes.find((palette) => palette.swatches.some((swatch) => swatch.id === swatchId));
}

/** True when the account can use none of the palette's swatches. */
export function isPaletteLocked(palette: Palette, locked: ReadonlySet<string>): boolean {
	return palette.swatches.length > 0 && palette.swatches.every((swatch) => locked.has(swatch.id));
}

/** The palette to open: the chosen swatch's, else the first one not locked. */
export function defaultPaletteId(
	palettes: readonly Palette[],
	swatchId: string | undefined,
	locked: ReadonlySet<string>
): string | undefined {
	const palette =
		paletteOfSwatch(palettes, swatchId) ??
		palettes.find((entry) => !isPaletteLocked(entry, locked)) ??
		palettes[0];
	return palette?.id;
}
