/**
 * @file cardFaceAtlas.ts
 * @brief Texture atlas service for card faces in WebGL/Three.js.
 *
 * Consolidates all card faces (front and back) into uniform-grid 2048x2048
 * canvas texture pages. Eliminates multi-mesh card rendering and allows single-mesh
 * sampling via normalized UV lookup rects.
 */

import { CanvasTexture, NearestFilter, SRGBColorSpace, type Texture } from "three";
import { CARD_COLOR_MAP } from "$lib/palette";
import { assetOrigin, isAllowedAssetUrl } from "./assetTrust";
import type { Card } from "$stores/game.svelte";

export interface AtlasEntry {
	page: number;
	u0: number;
	v0: number;
	u1: number;
	v1: number;
}

export interface CardFaceKey {
	type: string;
	value: string;
	wildColor?: string;
	turned: boolean;
	artVersion?: string;
}

/** @brief A single composited layer of a vanilla face. */
export type FaceLayer = "background" | "value" | "border";

export const ATLAS_PAGE_VERSION = { value: 0 };

const PAGE_SIZE = 2048;
const TILE_PAD = 2;
const CARD_PX_WIDTH = 43;
const CARD_PX_HEIGHT = 60;

const SLOT_WIDTH = CARD_PX_WIDTH + 2 * TILE_PAD; // 47px
const SLOT_HEIGHT = CARD_PX_HEIGHT + 2 * TILE_PAD; // 64px
const TILES_PER_ROW = Math.floor(PAGE_SIZE / SLOT_WIDTH); // 43
const TILES_PER_COL = Math.floor(PAGE_SIZE / SLOT_HEIGHT); // 32
const TILES_PER_PAGE = TILES_PER_ROW * TILES_PER_COL; // 1376

const STANDARD_ART_NAMES = [
	"background",
	"back",
	"border",
	"0",
	"1",
	"2",
	"3",
	"4",
	"5",
	"6",
	"7",
	"8",
	"9",
	"skip",
	"reverse",
	"+2",
	"jolly",
	"jolly_draw4"
] as const;

/**
 * @brief True when `name` is one of the pre-baked vanilla art assets.
 *
 * Only a `text` face whose label matches a standard asset can be rendered from
 * the existing atlas; modded `image`/`emoji`/`blank` faces have no baked
 * texture and are composed from their own layers.
 */
export function hasStandardFaceArt(name: string | undefined): name is string {
	return name !== undefined && (STANDARD_ART_NAMES as readonly string[]).includes(name);
}

/**
 * @brief Maps a card (plus its resolved defs face) onto an atlas face key.
 *
 * A `text` face supplies the color/label pair and the `art_version` that feeds
 * the face hash. Faces with no baked art — and cards with no defs entry at all
 * — fall back to the card's own `type`/`value`, which is exactly how vanilla
 * cards rendered before the defs table existed.
 */
export function cardFaceKeyFor(
	card: Pick<Card, "type" | "value" | "face">,
	wildColor: string | undefined,
	turned: boolean
): CardFaceKey {
	if (turned) {
		return { type: "wild", value: "0", turned: true };
	}
	const face = card.face;
	if (face && hasStandardFaceArt(face.label)) {
		return {
			type: face.color ?? card.type,
			value: face.label,
			wildColor,
			turned: false,
			artVersion: face.art_version !== undefined ? String(face.art_version) : undefined
		};
	}
	return { type: card.type, value: card.value, wildColor, turned: false };
}

interface AtlasPage {
	canvas: HTMLCanvasElement;
	ctx: CanvasRenderingContext2D | null;
	texture: CanvasTexture;
}

interface AllocatedSlot {
	key: CardFaceKey;
	hash: string;
	entry: AtlasEntry;
	page: number;
	drawX: number;
	drawY: number;
	baked: boolean;
	/** Set for a single-layer slot (`getLayerTexture`); undefined = composite. */
	layer?: FaceLayer;
}

const pages: AtlasPage[] = [];
const cache = new Map<string, AtlasEntry>();
const allocatedSlots: AllocatedSlot[] = [];
const loadedArt = new Map<string, CanvasImageSource>();

let scratchCanvas: HTMLCanvasElement | null = null;
let scratchCtx: CanvasRenderingContext2D | null = null;

function getScratchContext(w: number, h: number): CanvasRenderingContext2D | null {
	if (typeof document === "undefined") return null;
	if (!scratchCanvas) {
		scratchCanvas = document.createElement("canvas");
		scratchCanvas.width = w;
		scratchCanvas.height = h;
		scratchCtx = scratchCanvas.getContext("2d");
	} else if (scratchCanvas.width !== w || scratchCanvas.height !== h) {
		scratchCanvas.width = w;
		scratchCanvas.height = h;
		scratchCtx = scratchCanvas.getContext("2d");
	}
	return scratchCtx;
}

function ensurePage(pageIndex: number): AtlasPage {
	while (pages.length <= pageIndex) {
		const canvas =
			typeof document !== "undefined"
				? document.createElement("canvas")
				: ({ width: PAGE_SIZE, height: PAGE_SIZE } as HTMLCanvasElement);
		canvas.width = PAGE_SIZE;
		canvas.height = PAGE_SIZE;

		const ctx = canvas.getContext ? canvas.getContext("2d") : null;
		const texture = new CanvasTexture(canvas);
		texture.colorSpace = SRGBColorSpace;
		texture.magFilter = NearestFilter;
		texture.minFilter = NearestFilter;

		pages.push({ canvas, ctx, texture });
	}
	return pages[pageIndex];
}

// Ensure at least page 0 exists on module init
ensurePage(0);

/**
 * Deterministically hashes a CardFaceKey for atlas slot caching.
 * Turned cards share a single visual representation (back.png) per artVersion.
 */
export function faceKeyHash(key: CardFaceKey): string {
	if (key.turned) {
		return `back:${key.artVersion ?? ""}`;
	}
	return `front:${key.type}:${key.value}:${key.wildColor ?? ""}:${key.artVersion ?? ""}`;
}

export function atlasPageCount(): number {
	return pages.length;
}

export function getAtlasPage(page: number): Texture {
	return ensurePage(page).texture;
}

function drawTinted(
	destCtx: CanvasRenderingContext2D,
	img: CanvasImageSource,
	color: string,
	dx: number,
	dy: number,
	w: number,
	h: number
) {
	const sctx = getScratchContext(w, h);
	if (!sctx) {
		destCtx.drawImage(img, dx, dy, w, h);
		return;
	}
	sctx.clearRect(0, 0, w, h);
	sctx.globalCompositeOperation = "source-over";
	sctx.drawImage(img, 0, 0, w, h);
	sctx.globalCompositeOperation = "source-in";
	sctx.fillStyle = color;
	sctx.fillRect(0, 0, w, h);
	sctx.globalCompositeOperation = "source-over";

	destCtx.drawImage(sctx.canvas, dx, dy, w, h);
}

function canBakeKey(key: CardFaceKey): boolean {
	if (key.turned) {
		return loadedArt.has("back");
	}
	return loadedArt.has("background") && loadedArt.has(key.value) && loadedArt.has("border");
}

function bakeSlot(slot: AllocatedSlot): boolean {
	const page = ensurePage(slot.page);
	const ctx = page.ctx;
	if (!ctx) return false;

	const { key, drawX, drawY } = slot;
	const w = CARD_PX_WIDTH;
	const h = CARD_PX_HEIGHT;

	ctx.clearRect(drawX - TILE_PAD, drawY - TILE_PAD, SLOT_WIDTH, SLOT_HEIGHT);

	if (key.turned) {
		const backImg = loadedArt.get("back");
		if (backImg) {
			ctx.drawImage(backImg, drawX, drawY, w, h);
		}
	} else {
		// Layer 1: Background untinted
		const bgImg = loadedArt.get("background");
		if (bgImg) {
			ctx.drawImage(bgImg, drawX, drawY, w, h);
		}

		// Layer 2: Value (tinted unless unpainted jolly)
		const tintType = key.wildColor ?? key.type;
		const tintColorHex = CARD_COLOR_MAP[tintType] ?? "#ffffff";
		const paintedJolly = key.value === "jolly" && key.wildColor !== undefined;
		const shouldTint = key.value !== "jolly" || paintedJolly;

		const valImg = loadedArt.get(key.value);
		if (valImg) {
			if (shouldTint) {
				drawTinted(ctx, valImg, tintColorHex, drawX, drawY, w, h);
			} else {
				ctx.drawImage(valImg, drawX, drawY, w, h);
			}
		}

		// Layer 3: Border tinted
		const borderImg = loadedArt.get("border");
		if (borderImg) {
			drawTinted(ctx, borderImg, tintColorHex, drawX, drawY, w, h);
		}
	}

	page.texture.needsUpdate = true;
	slot.baked = true;
	return true;
}

/**
 * Returns the AtlasEntry UV rect for the given card face.
 * Bakes synchronously if art is loaded, or allocates slot and defers baking.
 */
export function getFaceTexture(key: CardFaceKey): AtlasEntry {
	const hash = faceKeyHash(key);
	const existing = cache.get(hash);
	if (existing) {
		return existing;
	}

	const { entry, slot } = allocateSlot(hash, key);

	if (canBakeKey(key)) {
		if (bakeSlot(slot)) {
			ATLAS_PAGE_VERSION.value++;
		}
	}

	return entry;
}

/** Allocates a fresh atlas slot (page/UV/slot bookkeeping) for `hash`. */
function allocateSlot(
	hash: string,
	key: CardFaceKey,
	layer?: FaceLayer
): { entry: AtlasEntry; slot: AllocatedSlot } {
	const slotIndex = allocatedSlots.length;
	const pageIndex = Math.floor(slotIndex / TILES_PER_PAGE);
	const slotInPage = slotIndex % TILES_PER_PAGE;
	const col = slotInPage % TILES_PER_ROW;
	const row = Math.floor(slotInPage / TILES_PER_ROW);

	const drawX = col * SLOT_WIDTH + TILE_PAD;
	const drawY = row * SLOT_HEIGHT + TILE_PAD;

	// In WebGL texture coordinates (with Three.js flipY = true):
	// v0 is bottom edge (canvas Y + H), v1 is top edge (canvas Y)
	const u0 = drawX / PAGE_SIZE;
	const u1 = (drawX + CARD_PX_WIDTH) / PAGE_SIZE;
	const v0 = (PAGE_SIZE - (drawY + CARD_PX_HEIGHT)) / PAGE_SIZE;
	const v1 = (PAGE_SIZE - drawY) / PAGE_SIZE;

	const entry: AtlasEntry = { page: pageIndex, u0, v0, u1, v1 };

	ensurePage(pageIndex);
	cache.set(hash, entry);

	const slot: AllocatedSlot = {
		key,
		hash,
		entry,
		page: pageIndex,
		drawX,
		drawY,
		baked: false,
		layer
	};
	allocatedSlots.push(slot);

	return { entry, slot };
}

function canBakeLayer(key: CardFaceKey, layer: FaceLayer): boolean {
	if (key.turned) return false;
	if (!loadedArt.has("background") || !loadedArt.has("border")) return false;
	if (layer === "value" && !loadedArt.has(key.value)) return false;
	return true;
}

function bakeLayerSlot(slot: AllocatedSlot, layer: FaceLayer): boolean {
	const page = ensurePage(slot.page);
	const ctx = page.ctx;
	if (!ctx) return false;

	const { key, drawX, drawY } = slot;
	const w = CARD_PX_WIDTH;
	const h = CARD_PX_HEIGHT;

	ctx.clearRect(drawX - TILE_PAD, drawY - TILE_PAD, SLOT_WIDTH, SLOT_HEIGHT);

	const tintType = key.wildColor ?? key.type;
	const tintColorHex = CARD_COLOR_MAP[tintType] ?? "#ffffff";

	if (layer === "background") {
		const bgImg = loadedArt.get("background");
		if (bgImg) ctx.drawImage(bgImg, drawX, drawY, w, h);
	} else if (layer === "value") {
		const paintedJolly = key.value === "jolly" && key.wildColor !== undefined;
		const shouldTint = key.value !== "jolly" || paintedJolly;
		const valImg = loadedArt.get(key.value);
		if (valImg) {
			if (shouldTint) {
				drawTinted(ctx, valImg, tintColorHex, drawX, drawY, w, h);
			} else {
				ctx.drawImage(valImg, drawX, drawY, w, h);
			}
		}
	} else {
		const borderImg = loadedArt.get("border");
		if (borderImg) drawTinted(ctx, borderImg, tintColorHex, drawX, drawY, w, h);
	}

	page.texture.needsUpdate = true;
	slot.baked = true;
	return true;
}

/**
 * @brief UV rect for one layer of a vanilla face.
 *
 * `inset` mod art is drawn between the background and the value/border, which
 * the single composite slot cannot express — so the renderer requests these
 * layers individually and stacks them around the art plane.
 */
export function getLayerTexture(layer: FaceLayer, key: CardFaceKey): AtlasEntry {
	const hash = `${faceKeyHash(key)}:${layer}`;
	const existing = cache.get(hash);
	if (existing) return existing;

	const { entry, slot } = allocateSlot(hash, key, layer);

	if (canBakeLayer(key, layer)) {
		if (bakeLayerSlot(slot, layer)) {
			ATLAS_PAGE_VERSION.value++;
		}
	}

	return entry;
}

async function loadArtImage(url: string): Promise<CanvasImageSource> {
	if (
		typeof navigator !== "undefined" &&
		typeof navigator.userAgent === "string" &&
		navigator.userAgent.includes("jsdom")
	) {
		if (typeof document !== "undefined") {
			const dummy = document.createElement("canvas");
			dummy.width = CARD_PX_WIDTH;
			dummy.height = CARD_PX_HEIGHT;
			return dummy;
		}
		return { width: CARD_PX_WIDTH, height: CARD_PX_HEIGHT } as CanvasImageSource;
	}

	if (!isAllowedAssetUrl(url, assetOrigin())) {
		throw new Error("asset URL origin not allowed");
	}
	const res = await fetch(url, { credentials: "omit" });
	if (!res.ok) throw new Error("asset fetch failed");
	const bytes = await res.arrayBuffer();
	const blobUrl = URL.createObjectURL(
		new Blob([bytes], { type: res.headers.get("content-type") ?? "image/png" })
	);
	try {
		return await new Promise<HTMLImageElement>((resolve, reject) => {
			const img = new Image();
			img.crossOrigin = "anonymous";
			img.onload = async () => {
				if ("decode" in img) {
					try {
						await img.decode();
					} catch {
						// Fallback if decode rejects
					}
				}
				resolve(img);
			};
			img.onerror = () => reject(new Error("asset image decode failed"));
			img.src = blobUrl;
		});
	} finally {
		URL.revokeObjectURL(blobUrl);
	}
}

let artLoadingPromise: Promise<void> | null = null;

/**
 * Pre-decodes all standard card art PNGs and bakes any pending atlas slots.
 * Safe for concurrent and repeated calls via singleton promise caching.
 */
export async function preloadCardArt(): Promise<void> {
	if (!artLoadingPromise) {
		artLoadingPromise = (async () => {
			const loadPromises = STANDARD_ART_NAMES.map(async (name) => {
				if (loadedArt.has(name)) return;
				try {
					const img = await loadArtImage(`/assets/cards/${name}.png`);
					loadedArt.set(name, img);
				} catch {
					// Ignore loading error for missing asset in fallback
				}
			});

			await Promise.all(loadPromises);
		})();
	}

	await artLoadingPromise;

	let newlyBaked = 0;
	for (const slot of allocatedSlots) {
		if (slot.baked) continue;
		const bakeable = slot.layer ? canBakeLayer(slot.key, slot.layer) : canBakeKey(slot.key);
		if (!bakeable) continue;
		const baked = slot.layer ? bakeLayerSlot(slot, slot.layer) : bakeSlot(slot);
		if (baked) {
			newlyBaked++;
		}
	}

	if (newlyBaked > 0) {
		ATLAS_PAGE_VERSION.value++;
	}
}
