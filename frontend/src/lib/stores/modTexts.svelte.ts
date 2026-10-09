/**
 * @file modTexts.svelte.ts
 * @brief The reactive face of the mod text layer.
 *
 * `revision` changes when a texts file loads, when a match installs a new
 * `defs` listing, and when the listing clears. A reader of `resolve` tracks the
 * revision and the active locale, so the play log and the glossary render again
 * after a locale change and after the new files arrive. Paraglide is the only
 * source of the active locale: the layer reads `getLocale()`, and the
 * interface store only tells this store WHEN to fetch.
 */

import { getLocale } from "$lib/paraglide/runtime.js";
import { ModTexts, fetchTextsBytes } from "$lib/i18n/modTexts";
import type { FormatParams } from "$lib/i18n/messageFormat";
import { storeI18n } from "./i18n.svelte";

class StoreModTexts {
	/** Changes when any text may differ from the last read. */
	revision = $state(0);

	#layer = new ModTexts({
		getLocale: () => getLocale(),
		fetchBytes: fetchTextsBytes,
		onChange: () => {
			this.revision += 1;
		}
	});

	constructor() {
		$effect.root(() => {
			$effect(() => {
				void storeI18n.locale;
				void this.#layer.sync();
			});
		});
	}

	/** Installs the texts listing of a `defs` packet and fetches the files. */
	ingestDefs(payload: unknown): void {
		const mods =
			typeof payload === "object" && payload !== null
				? (payload as { mods?: unknown }).mods
				: undefined;
		this.#layer.ingestMods(mods);
		this.revision += 1;
		void this.#layer.sync();
	}

	clear(): void {
		this.#layer.clear();
		this.revision += 1;
	}

	/** Drops the listing and the cached content. Used by tests. */
	reset(): void {
		this.#layer.forgetContent();
		this.clear();
	}

	/** The text of a namespaced key; null while loading or for a core key. */
	resolve(key: string, params: FormatParams): string | null {
		void this.revision;
		void storeI18n.locale;
		return this.#layer.resolve(key, params);
	}
}

export const storeModTexts = new StoreModTexts();
