export const FRAME_SIZE = 96;
export const FRAME_COUNT = 5;
export const FRAME_MS = 120;
export const STRIP_WIDTH = FRAME_SIZE * FRAME_COUNT; // 480
export const MAX_REGIONS = 3;

export type AvatarSlot =
	"shape" | "eyes" | "mouth" | "head_top" | "mask" | "bodywear" | "set" | "motif";
export type PartSlot = "eyes" | "mouth" | "head_top" | "mask" | "bodywear";
export type Socket = "eyes" | "mouth" | "head_top" | "body_centre";
export type Rarity = "common" | "uncommon" | "rare" | "epic";

export type ItemAsset =
	{ kind: "strip"; path: string } | { kind: "still"; path: string; bob: [number, number][] }; // 5 entries

export interface CatalogueItem {
	id: string; // "uni:eyes_dot"
	slot: AvatarSlot;
	name: string; // English fallback; i18n key is `avatar_item_<id with : -> _>`
	asset: ItemAsset;
	regions: 0 | 1 | 2 | 3;
	rarity: Rarity;
	price: number; // integer >= 0
	socket?: Socket; // parts in "still" format need it
	shapes?: string[]; // supported shape ids; undefined = all
	starter?: boolean;
	hidden?: boolean; // not shown in the shop, still equippable if owned
	unlisted?: boolean; // not shown until granted
	available_from?: string; // ISO UTC
	available_until?: string; // ISO UTC, exclusive
}

export interface Swatch {
	id: string;
	hex: string; // "#rrggbb"
}
export interface Palette {
	id: string;
	name: string;
	swatches: Swatch[];
	price: number;
	starter?: boolean;
}

export interface Catalogue {
	version: 1;
	shape_sockets: Record<string, Record<Socket, [number, number]>>;
	palettes: Palette[];
	items: CatalogueItem[];
	featured: string[];
}

export interface SlotChoice {
	item: string;
	colours: string[]; // swatch ids
}
export interface Loadout {
	shape: string; // shape item id
	skin: string; // swatch id
	slots: Partial<Record<Exclude<AvatarSlot, "shape">, SlotChoice>>;
}
