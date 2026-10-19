import type { Catalogue, CatalogueItem, Loadout } from "$lib/avatar/types";

/** Shared 5-frame bob, the same pattern the launch art uses. */
export const FIXTURE_BOB: [number, number][] = [
	[0, 0],
	[0, -1],
	[0, -2],
	[0, -1],
	[0, 0]
];

/**
 * Small catalogue for the bake tests: one item per slot, both asset formats,
 * a starter shape with sockets, and a set that blocks the parts.
 */
export function avatarFixture(): Catalogue {
	return {
		version: 1,
		shape_sockets: {
			"uni:round": { eyes: [10, 20], mouth: [12, 30], head_top: [8, 4], body_centre: [20, 60] }
		},
		palettes: [
			{
				id: "basic",
				name: "Basic",
				price: 0,
				starter: true,
				swatches: [
					{ id: "basic:red", hex: "#e84a4a" },
					{ id: "basic:green", hex: "#4aa85a" }
				]
			}
		],
		items: [
			{
				id: "uni:round",
				slot: "shape",
				name: "Round",
				asset: { kind: "strip", path: "shapes/uni_round.png" },
				regions: 1,
				rarity: "common",
				price: 0,
				starter: true
			},
			{
				id: "uni:eyes_dot",
				slot: "eyes",
				name: "Dot",
				asset: { kind: "strip", path: "eyes/uni_eyes_dot.png" },
				regions: 1,
				rarity: "common",
				price: 0,
				starter: true
			},
			{
				id: "uni:eyes_wide",
				slot: "eyes",
				name: "Wide",
				asset: { kind: "strip", path: "eyes/uni_eyes_wide.png" },
				regions: 1,
				rarity: "common",
				price: 50
			},
			{
				id: "uni:mouth_smile",
				slot: "mouth",
				name: "Smile",
				asset: { kind: "strip", path: "mouth/uni_mouth_smile.png" },
				regions: 1,
				rarity: "common",
				price: 0,
				starter: true
			},
			{
				id: "uni:hat_cap",
				slot: "head_top",
				name: "Cap",
				asset: { kind: "still", path: "head_top/uni_hat_cap.png", bob: FIXTURE_BOB },
				regions: 1,
				rarity: "common",
				price: 80,
				socket: "head_top"
			},
			{
				id: "uni:hat_float",
				slot: "head_top",
				name: "Floating",
				asset: { kind: "still", path: "head_top/uni_hat_float.png", bob: FIXTURE_BOB },
				regions: 1,
				rarity: "common",
				price: 80
			},
			{
				id: "uni:mask_bandit",
				slot: "mask",
				name: "Bandit",
				asset: { kind: "still", path: "mask/uni_mask_bandit.png", bob: FIXTURE_BOB },
				regions: 1,
				rarity: "uncommon",
				price: 150,
				socket: "eyes"
			},
			{
				id: "uni:body_scarf",
				slot: "bodywear",
				name: "Scarf",
				asset: { kind: "strip", path: "bodywear/uni_body_scarf.png" },
				regions: 1,
				rarity: "uncommon",
				price: 160
			},
			{
				id: "uni:set_suit",
				slot: "set",
				name: "Suit",
				asset: { kind: "strip", path: "set/uni_set_suit.png" },
				regions: 0,
				rarity: "rare",
				price: 900
			},
			{
				id: "uni:motif_patches",
				slot: "motif",
				name: "Patches",
				asset: { kind: "strip", path: "motif/uni_motif_patches.png" },
				regions: 3,
				rarity: "epic",
				price: 500
			}
		],
		featured: []
	};
}

/** A loadout that fills every slot, with per-test overrides. */
export function fullLoadout(overrides: Partial<Loadout> = {}): Loadout {
	const slots = {
		motif: { item: "uni:motif_patches", colours: ["basic:green", "basic:red"] },
		bodywear: { item: "uni:body_scarf", colours: [] },
		mouth: { item: "uni:mouth_smile", colours: [] },
		eyes: { item: "uni:eyes_dot", colours: [] },
		mask: { item: "uni:mask_bandit", colours: [] },
		head_top: { item: "uni:hat_cap", colours: [] }
	};

	return {
		shape: "uni:round",
		skin: "basic:red",
		...overrides,
		slots: { ...slots, ...overrides.slots }
	};
}

/**
 * A six-region motif with one extra mask file. It is NOT part of
 * `avatarFixture()`, so item counts in other tests stay the same. Tests push it
 * into the catalogue they install.
 */
export function sixRegionItem(): CatalogueItem {
	return {
		id: "uni:motif_rainbow",
		slot: "motif",
		name: "Rainbow",
		asset: {
			kind: "strip",
			path: "motif/uni_motif_rainbow.png",
			masks: ["motif/uni_motif_rainbow_b.png"]
		},
		regions: 6,
		rarity: "epic",
		price: 600
	};
}
