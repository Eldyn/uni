import {
	PersistentFaceStore,
	createIndexedDbFaceStoreBackend
} from "$components/game/three/atlasFaceStore";
import { decodePng } from "./browser";
import { bakeLoadoutPixels } from "./composite";
import type { PixelImage } from "./pixels";
import { loadoutHash, resolveLayers } from "./slotRules";
import type { Loadout } from "./types";
import { assetUrl } from "$lib/utils/assetUrl";

export interface BakeDeps {
	loadImage(path: string): Promise<PixelImage>;
	store?: PersistentFaceStore;
}

export interface BakedStrip {
	hash: string;
	image: PixelImage;
}

export interface BakeCache {
	get(loadout: Loadout): Promise<BakedStrip>;
	peek(loadout: Loadout): BakedStrip | undefined;
	clear(): void;
}

/**
 * Build a bake cache: a memory map of baked strips, an in-flight map that
 * shares one bake between concurrent gets, a memo of loaded art keyed by
 * asset path, and an optional persistent store so a repeat session reuses
 * the strip instead of re-baking.
 */
export function createBakeCache(deps: BakeDeps): BakeCache {
	const strips = new Map<string, BakedStrip>();
	const inFlight = new Map<string, Promise<BakedStrip>>();
	const images = new Map<string, Promise<PixelImage>>();

	function loadImage(path: string): Promise<PixelImage> {
		let pending = images.get(path);
		if (!pending) {
			// INFO: a failed load is evicted so the next bake retries it.
			pending = deps.loadImage(path).catch((error) => {
				images.delete(path);
				throw error;
			});
			images.set(path, pending);
		}
		return pending;
	}

	async function readPersisted(hash: string): Promise<PixelImage | undefined> {
		if (!deps.store) return undefined;
		const row = await deps.store.get(hash);
		if (!row) return undefined;
		return { width: row.width, height: row.height, data: new Uint8ClampedArray(row.data) };
	}

	async function bake(hash: string, loadout: Loadout): Promise<BakedStrip> {
		const layers = resolveLayers(loadout);
		const paths = [...new Set(layers.map((layer) => layer.item.asset.path))];

		const loaded = new Map<string, PixelImage>();
		await Promise.all(
			paths.map(async (path) => {
				loaded.set(path, await loadImage(path));
			})
		);

		const image = bakeLoadoutPixels(layers, loadout.shape, (path) => loaded.get(path));
		return { hash, image };
	}

	async function persist(strip: BakedStrip): Promise<void> {
		if (!deps.store) return;
		await deps.store
			.put({
				hash: strip.hash,
				width: strip.image.width,
				height: strip.image.height,
				data: new Uint8Array(strip.image.data)
			})
			.catch(() => {
				// WARN: persistence is best effort; the memory strip stands.
			});
	}

	function get(loadout: Loadout): Promise<BakedStrip> {
		const hash = loadoutHash(loadout);

		const cached = strips.get(hash);
		if (cached) return Promise.resolve(cached);

		const pending = inFlight.get(hash);
		if (pending) return pending;

		const run = (async () => {
			const stored = await readPersisted(hash);
			if (stored) {
				const strip = { hash, image: stored };
				strips.set(hash, strip);
				return strip;
			}

			const strip = await bake(hash, loadout);
			await persist(strip);
			strips.set(hash, strip);
			return strip;
		})();

		const tracked = run.finally(() => {
			inFlight.delete(hash);
		});
		inFlight.set(hash, tracked);
		return tracked;
	}

	return {
		get,
		peek: (loadout) => strips.get(loadoutHash(loadout)),
		clear: () => {
			strips.clear();
			inFlight.clear();
		}
	};
}

let singleton: BakeCache | undefined;

/**
 * The served URL for a catalogue-relative avatar asset path. INFO: the
 * catalogue stores paths relative to the avatar asset root, and assets live
 * under the hashed `public/assets/avatar/` directory, so a raw path must be
 * resolved here before it is fetched.
 */
export function avatarAssetUrl(path: string): string {
	return assetUrl(`/assets/avatar/${path}`);
}

/**
 * The app-wide bake cache, wired to the browser decode edge and the
 * IndexedDB store. INFO: created on first use, so importing this module
 * has no side effects (no store opened, no fetch).
 */
export function getBakeCache(): BakeCache {
	singleton ??= createBakeCache({
		loadImage: (path) => decodePng(avatarAssetUrl(path)),
		store: new PersistentFaceStore(createIndexedDbFaceStoreBackend())
	});
	return singleton;
}
