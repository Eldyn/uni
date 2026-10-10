/**
 * @file modGlossary.svelte.ts
 * @brief The reactive face of the mod glossary listing.
 *
 * `revision` changes when a match installs a new `defs` listing or clears it.
 * A reader of `entry` tracks the revision and, through the mod text layer, the
 * text revision and the active locale, so a tooltip renders again after a
 * locale change and after the texts arrive.
 */

import { ModGlossary } from "$lib/glossary/modGlossary";
import { storeModTexts } from "./modTexts.svelte";

export interface ResolvedModGlossaryEntry {
	id: string;
	title: string;
	description: string;
}

class StoreModGlossary {
	revision = $state(0);

	#registry = new ModGlossary();

	#catalog: unknown = undefined;
	#inMatch = false;

	#install(payload: unknown): void {
		const mods =
			typeof payload === "object" && payload !== null
				? (payload as { mods?: unknown }).mods
				: undefined;
		this.#registry.ingestMods(mods);
		this.revision += 1;
	}

	/** Installs the listing of the lobby `metadata` response, unless a match runs. */
	ingestCatalog(payload: unknown): void {
		this.#catalog = payload;
		if (!this.#inMatch) this.#install(payload);
	}

	ingestDefs(payload: unknown): void {
		this.#inMatch = true;
		this.#install(payload);
	}

	/** Ends the match listing. The catalog listing returns, when the lobby loaded one. */
	clear(): void {
		this.#inMatch = false;
		if (this.#catalog !== undefined) {
			this.#install(this.#catalog);
			return;
		}
		this.#registry.clear();
		this.revision += 1;
	}

	/** True when a mod lists the id. The texts may still load. */
	has(id: string): boolean {
		void this.revision;
		return this.#registry.get(id) !== undefined;
	}

	/** The entry with its texts; null for an unknown id or while texts load. */
	entry(id: string): ResolvedModGlossaryEntry | null {
		void this.revision;
		const entry = this.#registry.get(id);
		if (entry === undefined) return null;
		const resolveField = (field: string, value: string): string | null => {
			const key = this.#registry.keyOrNull(entry, field, value);
			return key === null ? value : storeModTexts.resolve(key, {});
		};
		const title = resolveField("label_key", entry.labelKey);
		const description = resolveField("description_key", entry.descriptionKey);
		if (title === null || description === null) return null;
		return { id, title, description };
	}

	/** Used by tests. */
	reset(): void {
		this.#catalog = undefined;
		this.clear();
	}
}

export const storeModGlossary = new StoreModGlossary();
