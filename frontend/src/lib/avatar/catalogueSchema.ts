import { z } from "zod";
import type { AvatarSlot, Catalogue, Socket } from "./types";
import { MAX_EXTRA_MASKS, RARITIES, REGIONS_PER_FILE } from "./types";

/** Slots a "still" asset must declare a socket for. */
const PART_SLOTS: readonly AvatarSlot[] = ["eyes", "mouth", "head_top", "mask", "bodywear"];
/** Slots whose coverage needs a starter item (shape, eyes, mouth). */
const STARTER_SLOTS: readonly AvatarSlot[] = ["shape", "eyes", "mouth"];
const SOCKETS: readonly Socket[] = ["eyes", "mouth", "head_top", "body_center"];
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
		body_center: socketOffset
	})
);

const extraMasks = z.array(z.string()).max(MAX_EXTRA_MASKS).optional();

const itemAssetSchema = z.discriminatedUnion("kind", [
	z.object({ kind: z.literal("strip"), path: z.string(), masks: extraMasks }),
	z.object({
		kind: z.literal("still"),
		path: z.string(),
		bob: z.array(z.tuple([z.number(), z.number()])).length(5),
		masks: extraMasks
	})
]);

const itemSchema = z.object({
	id: z.string().regex(NAMESPACED_ID, "id must be namespaced, e.g. uni:eyes_dot"),
	slot: z.enum(AVATAR_SLOTS),
	name: z.string(),
	asset: itemAssetSchema,
	regions: z.union([
		z.literal(0),
		z.literal(1),
		z.literal(2),
		z.literal(3),
		z.literal(4),
		z.literal(5),
		z.literal(6)
	]),
	tint: z.enum(["mask", "multiply"]).optional(),
	default_colors: z.array(z.string()).optional(),
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
	hex: z.string().regex(HEX, "hex must be #rrggbb"),
	price: z.number().int().nonnegative().optional(),
	rarity: z.enum(RARITIES).optional(),
	hidden: z.boolean().optional(),
	unlisted: z.boolean().optional()
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

	const swatchIds = new Set(
		catalogue.palettes.flatMap((palette) => palette.swatches.map((swatch) => swatch.id))
	);

	catalogue.items.forEach((item, index) => {
		if (item.asset.kind === "still" && PART_SLOTS.includes(item.slot) && !item.socket) {
			ctx.addIssue({
				code: "custom",
				message: `still item "${item.id}" needs a socket`,
				path: ["items", index, "socket"]
			});
		}
		const capacity = REGIONS_PER_FILE * (1 + (item.asset.masks?.length ?? 0));
		if (item.regions > capacity) {
			ctx.addIssue({
				code: "custom",
				message: `item "${item.id}" has ${item.regions} regions but its files carry ${capacity}`,
				path: ["items", index, "regions"]
			});
		}
		if (item.tint === "multiply") {
			if (item.regions !== 1) {
				ctx.addIssue({
					code: "custom",
					message: `multiply item "${item.id}" needs exactly 1 region`,
					path: ["items", index, "tint"]
				});
			}
			if ((item.asset.masks?.length ?? 0) > 0) {
				ctx.addIssue({
					code: "custom",
					message: `multiply item "${item.id}" takes no extra mask files`,
					path: ["items", index, "tint"]
				});
			}
		}
		if (item.default_colors !== undefined) {
			const limit =
				item.slot === "shape"
					? 0
					: item.slot === "motif"
						? Math.max(0, item.regions - 1)
						: item.regions;
			if (item.slot === "shape" || item.default_colors.length > limit) {
				ctx.addIssue({
					code: "custom",
					message: `item "${item.id}" holds at most ${limit} default colors`,
					path: ["items", index, "default_colors"]
				});
			}
			item.default_colors.forEach((entry, entryIndex) => {
				if (HEX.test(entry) || swatchIds.has(entry)) return;
				ctx.addIssue({
					code: "custom",
					message: `default color "${entry}" is neither #rrggbb nor a swatch id`,
					path: ["items", index, "default_colors", entryIndex]
				});
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
		if (seenIds.has(palette.id)) {
			ctx.addIssue({
				code: "custom",
				message: `duplicate catalogue id "${palette.id}"`,
				path: ["palettes", paletteIndex, "id"]
			});
		}
		seenIds.add(palette.id);
		palette.swatches.forEach((swatch, swatchIndex) => {
			if (seenSwatches.has(swatch.id)) {
				ctx.addIssue({
					code: "custom",
					message: `duplicate swatch id "${swatch.id}"`,
					path: ["palettes", paletteIndex, "swatches", swatchIndex, "id"]
				});
			}
			if (seenIds.has(swatch.id)) {
				ctx.addIssue({
					code: "custom",
					message: `duplicate catalogue id "${swatch.id}"`,
					path: ["palettes", paletteIndex, "swatches", swatchIndex, "id"]
				});
			}
			seenSwatches.add(swatch.id);
			seenIds.add(swatch.id);
		});
	});
}

export const CatalogueSchema = baseCatalogueSchema.superRefine(refineCatalogue);

/** Parse raw JSON into a validated catalogue; throws `ZodError` with path info. */
export function parseCatalogue(raw: unknown): Catalogue {
	return CatalogueSchema.parse(raw);
}
