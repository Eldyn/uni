import { z } from "zod";
import type { AvatarSlot, Catalogue, Rarity, Socket } from "./types";

/** Slots a "still" asset must declare a socket for. */
const PART_SLOTS: readonly AvatarSlot[] = ["eyes", "mouth", "head_top", "mask", "bodywear"];
/** Slots whose coverage needs a starter item (shape, eyes, mouth). */
const STARTER_SLOTS: readonly AvatarSlot[] = ["shape", "eyes", "mouth"];
const SOCKETS: readonly Socket[] = ["eyes", "mouth", "head_top", "body_centre"];
const AVATAR_SLOTS: readonly AvatarSlot[] = [
	"shape",
	"eyes",
	"mouth",
	"head_top",
	"mask",
	"bodywear",
	"set",
	"motif"
];
const RARITIES: readonly Rarity[] = ["common", "uncommon", "rare", "epic"];

/** A namespaced id is `<namespace>:<name>`; both parts must be non-empty. */
const NAMESPACED_ID = /^[^:]+:.+$/;
const HEX = /^#[0-9a-fA-F]{6}$/;

const socketOffset = z.tuple([z.number(), z.number()]);

const shapeSocketsSchema = z.record(
	z.string(),
	z.object({
		eyes: socketOffset,
		mouth: socketOffset,
		head_top: socketOffset,
		body_centre: socketOffset
	})
);

const itemAssetSchema = z.discriminatedUnion("kind", [
	z.object({ kind: z.literal("strip"), path: z.string() }),
	z.object({
		kind: z.literal("still"),
		path: z.string(),
		bob: z.array(z.tuple([z.number(), z.number()])).length(5)
	})
]);

const itemSchema = z.object({
	id: z.string().regex(NAMESPACED_ID, "id must be namespaced, e.g. uni:eyes_dot"),
	slot: z.enum(AVATAR_SLOTS),
	name: z.string(),
	asset: itemAssetSchema,
	regions: z.union([z.literal(0), z.literal(1), z.literal(2), z.literal(3)]),
	rarity: z.enum(RARITIES),
	price: z.number().int().nonnegative(),
	socket: z.enum(SOCKETS).optional(),
	shapes: z.array(z.string()).optional(),
	starter: z.boolean().optional(),
	hidden: z.boolean().optional(),
	unlisted: z.boolean().optional(),
	available_from: z.string().optional(),
	available_until: z.string().optional()
});

const swatchSchema = z.object({
	id: z.string(),
	hex: z.string().regex(HEX, "hex must be #rrggbb")
});

const paletteSchema = z.object({
	id: z.string(),
	name: z.string(),
	swatches: z.array(swatchSchema),
	price: z.number().int().nonnegative(),
	starter: z.boolean().optional()
});

const baseCatalogueSchema = z.object({
	version: z.literal(1),
	shape_sockets: shapeSocketsSchema,
	palettes: z.array(paletteSchema),
	items: z.array(itemSchema),
	featured: z.array(z.string())
});

/** Cross-field catalogue rules: ids, starters, featured, sockets, windows. */
function refineCatalogue(catalogue: z.infer<typeof baseCatalogueSchema>, ctx: z.RefinementCtx) {
	const seenIds = new Set<string>();
	catalogue.items.forEach((item, index) => {
		if (seenIds.has(item.id)) {
			ctx.addIssue({
				code: "custom",
				message: `duplicate item id "${item.id}"`,
				path: ["items", index, "id"]
			});
		}
		seenIds.add(item.id);
	});

	for (const slot of STARTER_SLOTS) {
		if (!catalogue.items.some((item) => item.slot === slot && item.starter === true)) {
			ctx.addIssue({
				code: "custom",
				message: `slot "${slot}" needs a starter item`,
				path: ["items"]
			});
		}
	}

	catalogue.featured.forEach((id, index) => {
		if (!seenIds.has(id)) {
			ctx.addIssue({
				code: "custom",
				message: `featured id "${id}" is not an item`,
				path: ["featured", index]
			});
		}
	});

	catalogue.items.forEach((item, index) => {
		if (item.asset.kind === "still" && PART_SLOTS.includes(item.slot) && !item.socket) {
			ctx.addIssue({
				code: "custom",
				message: `still item "${item.id}" needs a socket`,
				path: ["items", index, "socket"]
			});
		}
		if (item.available_from && item.available_until && item.available_until < item.available_from) {
			ctx.addIssue({
				code: "custom",
				message: `item "${item.id}" available_until is before available_from`,
				path: ["items", index, "available_until"]
			});
		}
	});

	const seenSwatches = new Set<string>();
	catalogue.palettes.forEach((palette, paletteIndex) => {
		palette.swatches.forEach((swatch, swatchIndex) => {
			if (seenSwatches.has(swatch.id)) {
				ctx.addIssue({
					code: "custom",
					message: `duplicate swatch id "${swatch.id}"`,
					path: ["palettes", paletteIndex, "swatches", swatchIndex, "id"]
				});
			}
			seenSwatches.add(swatch.id);
		});
	});
}

export const CatalogueSchema = baseCatalogueSchema.superRefine(refineCatalogue);

/** Parse raw JSON into a validated catalogue; throws `ZodError` with path info. */
export function parseCatalogue(raw: unknown): Catalogue {
	return CatalogueSchema.parse(raw);
}
