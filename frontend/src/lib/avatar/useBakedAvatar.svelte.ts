import { getBakeCache } from "./bakeCache";
import { toCanvas } from "./browser";
import { defaultLoadout } from "./defaultLoadout";
import type { PixelImage } from "./pixels";
import { loadoutHash } from "./slotRules";
import type { Loadout } from "./types";

/** The slice of the bake cache the helper needs. */
export interface BakeCacheLike {
	get(loadout: Loadout): Promise<{ image: PixelImage }>;
}

export interface BakedAvatarOptions {
	cache?: BakeCacheLike;
	toBlobUrl?: (img: PixelImage) => Promise<string>;
}

export interface BakedAvatar {
	readonly url: string | null;
	set(loadout: Loadout | null | undefined): void;
	destroy(): void;
}

async function defaultToBlobUrl(img: PixelImage): Promise<string> {
	const canvas = toCanvas(img);
	const blob = await new Promise<Blob | null>((resolve) => canvas.toBlob(resolve, "image/png"));
	if (!blob) throw new Error("avatar strip export failed");
	return URL.createObjectURL(blob);
}

/**
 * Bake a loadout into an object URL. INFO: the URL starts null until the
 * first bake resolves; equal loadouts reuse the previous bake and a failed
 * bake leaves the previous strip on screen.
 */
export function createBakedAvatar(opts: BakedAvatarOptions = {}): BakedAvatar {
	const cache = opts.cache ?? getBakeCache();
	const toBlobUrl = opts.toBlobUrl ?? defaultToBlobUrl;

	let url = $state<string | null>(null);
	let lastHash: string | null = null;
	let requested = 0;
	let destroyed = false;

	function revoke(value: string | null): void {
		if (value) URL.revokeObjectURL(value);
	}

	function set(loadout: Loadout | null | undefined): void {
		const next = loadout ?? defaultLoadout();
		const hash = loadoutHash(next);
		if (hash === lastHash) return;

		lastHash = hash;
		const token = ++requested;

		cache
			.get(next)
			.then((strip) => toBlobUrl(strip.image))
			.then((nextUrl) => {
				// INFO: a stale or unmounted bake must not replace the strip.
				if (destroyed || token !== requested) {
					revoke(nextUrl);
					return;
				}

				const previous = url;
				url = nextUrl;
				if (previous && previous !== nextUrl) revoke(previous);
			})
			.catch(() => {
				// WARN: a failed bake keeps the previous strip on screen.
			});
	}

	function destroy(): void {
		destroyed = true;
		requested++;
		revoke(url);
		url = null;
	}

	return {
		get url() {
			return url;
		},
		set,
		destroy
	};
}
