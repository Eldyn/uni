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
import { assetUrl } from "$lib/utils/assetUrl";
import { cardBackUrl, cardFaceUrl } from "$lib/cards/skinPaths";
import type { FaceOverride } from "$components/game/affliction/overrideChannel";
import {
	PersistentFaceStore,
	createIndexedDbFaceStoreBackend,
	type StoredFace
} from "./atlasFaceStore";

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
	/** Equipped `card_face` item id; part of the front hash so a skin change re-bakes. */
	skinId?: string | null;
	/** Equipped `card_back` item id; part of the turned (back) hash. */
	backId?: string | null;
}

/** @brief A single composited layer of a default face. */
export type FaceLayer = "background" | "value" | "border";

export const ATLAS_PAGE_VERSION = { value: 0 };

const PAGE_SIZE = 2048;
const TILE_PAD = 2;
const CARD_PX_WIDTH = 43;
const CARD_PX_HEIGHT = 60;

/** Native pixel dimensions of one baked face — the grid decorative overlays
 *  (e.g. the playable-card glint) snap to so they read as part of the art. */
export const CARD_ART_PX = { width: CARD_PX_WIDTH, height: CARD_PX_HEIGHT } as const;

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

/** One standard art name and the URLs to try for it, skinned first. */
export interface StandardArtSource {
	name: string;
	/** Ordered candidates: the equipped skin's file, then the base file. */
	urls: string[];
}

/**
 * @brief Keys `loadedArt` by the skin the pixels were requested for.
 *
 * A key without `skinId`/`backId` keeps the bare name, so the default art keeps
 * the exact cache keys (and atlas hashes) it had before skins existed.
 */
function loadedArtKey(name: string, skinId?: string | null, backId?: string | null): string {
	if (name === "back") return backId ? `back:${backId}` : "back";
	return skinId ? `${name}:${skinId}` : name;
}

/**
 * @brief The URL candidates for every standard art name under the equipped skins.
 *
 * A `card_face` skin is a folder of per-value PNGs, so each front name is tried
 * from that folder first and falls back to the base file. The back tile is a
 * single `card_back` PNG instead, and never comes from the face folder.
 */
export function standardArtUrls(
	skinId?: string | null,
	backId?: string | null
): StandardArtSource[] {
	return STANDARD_ART_NAMES.map((name) => {
		const base = assetUrl(`/assets/cards/${name}.png`);
		const skinned =
			name === "back"
				? backId
					? cardBackUrl(backId)
					: null
				: skinId
					? cardFaceUrl(name, skinId)
					: null;
		return { name, urls: skinned && skinned !== base ? [skinned, base] : [base] };
	});
}

/**
 * @brief True when `name` is one of the pre-baked default art assets.
 *
 * Only a `text` face whose label matches a standard asset can be rendered from
 * the existing atlas; modded `image`/`emoji`/`blank` faces have no baked
 * texture and are composed from their own layers.
 */
export function hasStandardFaceArt(name: string | undefined): name is string {
	return name !== undefined && (STANDARD_ART_NAMES as readonly string[]).includes(name);
}

/**
 * @brief The default face values a `seeded-random-tile` face source may pick
 * from. Excludes the non-value layers (`background`/`back`/`border`)
 * so a random tile is always a legible face.
 */
export const STANDARD_TILE_VALUES: readonly string[] = STANDARD_ART_NAMES.filter(
	(name) => name !== "background" && name !== "back" && name !== "border"
);

/**
 * @brief The face key a renderer resolves after an active `face.override`.
 *
 * is UV/texture-only: an override's `texture` names the face value to
 * draw, so the atlas bakes/resolves that face instead of the card's own. The
 * card back is never overridden (a turned card's face is not the front), and an
 * override without a `texture` leaves the key untouched (its `uvRect`, if any,
 * is applied by `entryWithOverride`).
 */
export function faceKeyForOverride(
	base: CardFaceKey,
	override: FaceOverride | undefined
): CardFaceKey {
	if (base.turned || !override?.texture) return base;
	return { ...base, value: override.texture };
}

/**
 * @brief The atlas entry a renderer draws after an active `face.override`.
 *
 * An override `uvRect` is a `[u0, v0, w, h]` rect in normalized atlas space
 * (the same convention as the shader's `uUvRectFront`), applied onto the base
 * entry's page. Without a `uvRect` the base entry is returned unchanged.
 */
export function entryWithOverride(
	base: AtlasEntry,
	override: FaceOverride | undefined
): AtlasEntry {
	const rect = override?.uvRect;
	if (!rect) return base;
	const [u0, v0, w, h] = rect;
	return { ...base, u0, v0, u1: u0 + w, v1: v0 + h };
}

/**
 * @brief Maps a card (plus its resolved defs face) onto an atlas face key.
 *
 * A `text` face supplies the color/label pair and the `art_version` that feeds
 * the face hash. Faces with no baked art — and cards with no defs entry at all
 * — fall back to the card's own `type`/`value`, which is exactly how default
 * cards rendered before the defs table existed.
 *
 * The optional equipped skin ids join the key, so an equipped `card_face`
 * re-bakes the front and an equipped `card_back` re-bakes the back.
 */
export function cardFaceKeyFor(
	card: Pick<Card, "type" | "value" | "face">,
	wildColor: string | undefined,
	turned: boolean,
	skinId?: string | null,
	backId?: string | null
): CardFaceKey {
	if (turned) {
		const backKey: CardFaceKey = { type: "wild", value: "0", turned: true };
		if (backId) backKey.backId = backId;
		return backKey;
	}
	const face = card.face;
	if (face && hasStandardFaceArt(face.label)) {
		return {
			type: face.color ?? card.type,
			value: face.label,
			wildColor,
			turned: false,
			artVersion: face.art_version !== undefined ? String(face.art_version) : undefined,
			...(skinId ? { skinId } : {})
		};
	}
	return {
		type: card.type,
		value: card.value,
		wildColor,
		turned: false,
		...(skinId ? { skinId } : {})
	};
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

const FACE_LAYERS: FaceLayer[] = ["background", "value", "border"];

/** What the atlas stores beside a face's pixels, to re-place them later. */
export interface PersistedFaceMeta {
	key: CardFaceKey;
	layer?: FaceLayer;
}

let faceStore: PersistentFaceStore | null = null;
let hydrationPromise: Promise<void> | null = null;

function getFaceStore(): PersistentFaceStore {
	// INFO: opened lazily; without IndexedDB (tests, SSR) the backend falls
	// back to memory, so the seam stays inert instead of throwing.
	faceStore ??= new PersistentFaceStore(createIndexedDbFaceStoreBackend());
	return faceStore;
}

/**
 * @brief Swaps the persistent face store (tests / optional DI).
 *
 * Also restarts hydration so the next `hydrateAtlasFaces` reads the new
 * store instead of reusing the previous singleton promise.
 */
export function setAtlasFaceStore(store: PersistentFaceStore): void {
	faceStore = store;
	hydrationPromise = null;
}

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
 * The equipped skin ids join the hash so changing a skin allocates fresh slots.
 */
export function faceKeyHash(key: CardFaceKey): string {
	const skin = key.skinId ? `:${key.skinId}` : "";
	const back = key.backId ? `:${key.backId}` : "";
	if (key.turned) {
		return `back:${key.artVersion ?? ""}${back}`;
	}
	return `front:${key.type}:${key.value}:${key.wildColor ?? ""}:${key.artVersion ?? ""}${skin}`;
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

/** The `loadedArt` key for one standard name under a face key's equipped skins. */
function artKeyFor(key: CardFaceKey, name: string): string {
	return loadedArtKey(name, key.skinId, key.backId);
}

function canBakeKey(key: CardFaceKey): boolean {
	if (key.turned) {
		return loadedArt.has(artKeyFor(key, "back"));
	}
	return (
		loadedArt.has(artKeyFor(key, "background")) &&
		loadedArt.has(artKeyFor(key, key.value)) &&
		loadedArt.has(artKeyFor(key, "border"))
	);
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
		const backImg = loadedArt.get(artKeyFor(key, "back"));
		if (backImg) {
			ctx.drawImage(backImg, drawX, drawY, w, h);
		}
	} else {
		// Layer 1: Background untinted
		const bgImg = loadedArt.get(artKeyFor(key, "background"));
		if (bgImg) {
			ctx.drawImage(bgImg, drawX, drawY, w, h);
		}

		// Layer 2: Value (tinted unless unpainted jolly)
		const tintType = key.wildColor ?? key.type;
		const tintColorHex = CARD_COLOR_MAP[tintType] ?? "#ffffff";
		const paintedJolly = key.value === "jolly" && key.wildColor !== undefined;
		const shouldTint = key.value !== "jolly" || paintedJolly;

		const valImg = loadedArt.get(artKeyFor(key, key.value));
		if (valImg) {
			if (shouldTint) {
				drawTinted(ctx, valImg, tintColorHex, drawX, drawY, w, h);
			} else {
				ctx.drawImage(valImg, drawX, drawY, w, h);
			}
		}

		// Layer 3: Border tinted
		const borderImg = loadedArt.get(artKeyFor(key, "border"));
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
			persistSlot(slot);
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

/**
 * @brief Paints stored pixels into an allocated slot.
 *
 * Returns false when the environment cannot paint (no 2D context) or the
 * record's geometry no longer matches the slot; the slot then stays unbaked
 * and `preloadCardArt` may still re-bake it from source art as a fallback.
 */
function paintSlot(slot: AllocatedSlot, row: StoredFace): boolean {
	const ctx = pages[slot.page]?.ctx;
	if (!ctx || typeof ImageData === "undefined") return false;
	if (row.width !== CARD_PX_WIDTH || row.height !== CARD_PX_HEIGHT) return false;
	let pixels: ImageData;
	try {
		pixels = new ImageData(new Uint8ClampedArray(row.data), row.width, row.height);
	} catch {
		// WARN: malformed pixel payload — fall back to a source-art bake.
		return false;
	}
	ctx.putImageData(pixels, slot.drawX, slot.drawY);
	pages[slot.page].texture.needsUpdate = true;
	slot.baked = true;
	return true;
}

/**
 * @brief Fire-and-forget write-back of a freshly baked slot.
 *
 * INFO: pixels go out as raw RGBA rather than PNG — the simplest encoding
 * that needs no compressor dependency, at ~10 KB per face.
 */
function persistSlot(slot: AllocatedSlot): void {
	const ctx = pages[slot.page]?.ctx;
	if (!ctx) return;
	let data: Uint8Array;
	try {
		const pixels = ctx.getImageData(slot.drawX, slot.drawY, CARD_PX_WIDTH, CARD_PX_HEIGHT);
		data = new Uint8Array(pixels.data);
	} catch {
		// WARN: unreadable canvas — this face stays memory-only.
		return;
	}
	const meta: PersistedFaceMeta = { key: slot.key, layer: slot.layer };
	getFaceStore()
		.put({ hash: slot.hash, width: CARD_PX_WIDTH, height: CARD_PX_HEIGHT, data, meta })
		.catch(() => {
			// WARN: persistence is best effort; the in-memory bake stands.
		});
}

/**
 * @brief Re-materializes persisted faces into fresh atlas slots.
 *
 * Hydrated hashes land in the in-memory cache, so `getFaceTexture` serves
 * them without re-baking. A record that cannot be painted keeps its slot
 * unbaked and the source-art bake in `preloadCardArt` remains the fallback.
 */
export function hydrateAtlasFaces(): Promise<void> {
	hydrationPromise ??= (async () => {
		const rows = await getFaceStore().entries();
		let painted = 0;
		for (const row of rows) {
			if (cache.has(row.hash)) continue;
			const meta = row.meta as PersistedFaceMeta | undefined;
			if (!meta?.key) continue;
			const { slot } = allocateSlot(row.hash, meta.key, meta.layer);
			if (paintSlot(slot, row)) painted++;
		}
		if (painted > 0) ATLAS_PAGE_VERSION.value++;
	})().catch(() => {
		// WARN: a failed read must not block load; the next preload retries.
		hydrationPromise = null;
	});
	return hydrationPromise;
}

/**
 * @brief Drops persisted faces whose hash the live card pool no longer makes.
 *
 * INFO: the atlas cannot know the card pool, so the defs/mod layer passes
 * the live content hashes (faceKeyHash outputs) once a mod or art change
 * resolves; layer variants of a live hash are kept with the composite one.
 */
export function pruneStaleAtlasFaces(liveHashes: Iterable<string>): Promise<number> {
	const live = new Set<string>();
	for (const hash of liveHashes) {
		live.add(hash);
		for (const layer of FACE_LAYERS) live.add(`${hash}:${layer}`);
	}
	return getFaceStore().prune(live);
}

function canBakeLayer(key: CardFaceKey, layer: FaceLayer): boolean {
	if (key.turned) return false;
	if (!loadedArt.has(artKeyFor(key, "background")) || !loadedArt.has(artKeyFor(key, "border")))
		return false;
	if (layer === "value" && !loadedArt.has(artKeyFor(key, key.value))) return false;
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
		const bgImg = loadedArt.get(artKeyFor(key, "background"));
		if (bgImg) ctx.drawImage(bgImg, drawX, drawY, w, h);
	} else if (layer === "value") {
		const paintedJolly = key.value === "jolly" && key.wildColor !== undefined;
		const shouldTint = key.value !== "jolly" || paintedJolly;
		const valImg = loadedArt.get(artKeyFor(key, key.value));
		if (valImg) {
			if (shouldTint) {
				drawTinted(ctx, valImg, tintColorHex, drawX, drawY, w, h);
			} else {
				ctx.drawImage(valImg, drawX, drawY, w, h);
			}
		}
	} else {
		const borderImg = loadedArt.get(artKeyFor(key, "border"));
		if (borderImg) drawTinted(ctx, borderImg, tintColorHex, drawX, drawY, w, h);
	}

	page.texture.needsUpdate = true;
	slot.baked = true;
	return true;
}

/**
 * @brief UV rect for one layer of a default face.
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
			persistSlot(slot);
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

/** Decodes the first URL that loads; skinned art falls back to the base file. */
export async function loadFirstArtSource(
	urls: readonly string[],
	load: (url: string) => Promise<CanvasImageSource> = loadArtImage
): Promise<CanvasImageSource> {
	let failure: unknown;
	for (const url of urls) {
		try {
			return await load(url);
		} catch (err) {
			failure ??= err;
		}
	}
	throw failure ?? new Error("no card art source");
}

/** One in-flight decode per equipped skin pair. */
const artLoadingPromises = new Map<string, Promise<void>>();

/**
 * Pre-decodes all standard card art PNGs and bakes any pending atlas slots.
 * Safe for concurrent and repeated calls: each equipped skin pair shares one
 * singleton promise, and a changed pair loads and bakes its own art.
 */
export async function preloadCardArt(
	skinId?: string | null,
	backId?: string | null
): Promise<void> {
	await hydrateAtlasFaces();

	const variant = `${skinId ?? ""}|${backId ?? ""}`;
	let artLoadingPromise = artLoadingPromises.get(variant);
	if (!artLoadingPromise) {
		artLoadingPromise = (async () => {
			const loadPromises = standardArtUrls(skinId, backId).map(async ({ name, urls }) => {
				const key = loadedArtKey(name, skinId, backId);
				if (loadedArt.has(key)) return;
				try {
					loadedArt.set(key, await loadFirstArtSource(urls));
				} catch {
					// Ignore loading error for missing asset in fallback
				}
			});

			await Promise.all(loadPromises);
		})();
		artLoadingPromises.set(variant, artLoadingPromise);
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
			persistSlot(slot);
		}
	}

	if (newlyBaked > 0) {
		ATLAS_PAGE_VERSION.value++;
	}
}
