import { getCatalogue, slotDefault } from "./catalogue";
import type { Loadout } from "./types";

/**
 * The local fallback look: the starter shape, the first swatch of the basic
 * palette, the starter eyes and mouth, and no other slots. INFO: used until
 * a real loadout arrives and for players from older servers.
 */
export function defaultLoadout(): Loadout {
	const catalogue = getCatalogue();
	const basic =
		catalogue.palettes.find((palette) => palette.id === "basic") ??
		catalogue.palettes.find((palette) => palette.starter === true) ??
		catalogue.palettes[0];

	const slots: Loadout["slots"] = {};
	const eyes = slotDefault("eyes");
	const mouth = slotDefault("mouth");
	if (eyes) slots.eyes = { item: eyes.id, colours: [] };
	if (mouth) slots.mouth = { item: mouth.id, colours: [] };

	return {
		shape: slotDefault("shape")?.id ?? "",
		skin: basic?.swatches[0]?.id ?? "",
		slots
	};
}
