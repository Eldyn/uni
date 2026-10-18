import { z } from "zod";
import type { Loadout } from "./types";

/** One equipped slot: the item id and the swatch ids it is tinted with. */
export const SlotChoiceSchema = z.looseObject({
	item: z.string(),
	colours: z.array(z.string())
});

/**
 * Wire shape of a saved loadout. Loose, like the other incoming-payload
 * schemas: catalogues can grow fields the client does not read yet.
 */
export const LoadoutSchema = z.looseObject({
	shape: z.string(),
	skin: z.string(),
	slots: z.record(z.string(), SlotChoiceSchema)
});

/**
 * @brief Parse a raw loadout into a `Loadout`, or null when it does not fit
 * the shape. Never throws: a malformed loadout must not break a lobby or
 * snapshot payload, it just renders with the slot defaults.
 */
export function parseLoadout(raw: unknown): Loadout | null {
	if (raw === null || raw === undefined) return null;
	const parsed = LoadoutSchema.safeParse(raw);
	return parsed.success ? (parsed.data as Loadout) : null;
}
