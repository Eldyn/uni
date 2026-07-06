/**
 * @file catalog.svelte.ts
 * @brief Lazy-loaded static server metadata (rule catalog).
 * Fetched once per session via the dedicated `metadata_request` action so
 * every consumer (browse filters, lobby settings) reads the same data instead
 * of each response re-attaching it.
 */

import { z } from "zod";
import { storeToast } from "./toast.svelte";
import { ClientAction, ws } from "./ws.svelte";

/**
 * @interface RuleDefinition
 * @brief Describes a single game rule as provided by the server.
 */
export interface RuleDefinition {
	/** Unique identifier used in active_mods (e.g. "seven_zero"). */
	id: string;
	/** Short human-readable name for UI display. */
	label: string;
	/** One-sentence explanation of the rule's effect. */
	description: string;
}

const RuleDefinitionSchema = z.looseObject({
	id: z.string(),
	label: z.string(),
	description: z.string()
});

/**
 * @class StoreCatalog
 * @brief Singleton store holding server-provided static metadata.
 * @tag FRONT-CATALOG-001
 */
class StoreCatalog {
	/** Rule definitions supported by this server instance. */
	rules = $state<RuleDefinition[]>([]);

	#loadPromise: Promise<void> | null = null;

	/**
	 * @brief Loads the metadata once; concurrent callers share the same request.
	 * Failed loads clear the in-flight promise so a later call can retry.
	 * @tag FRONT-CATALOG-MTH-001
	 */
	ensureLoaded(): Promise<void> {
		if (this.rules.length > 0) return Promise.resolve();
		if (this.#loadPromise) return this.#loadPromise;

		this.#loadPromise = this.#fetch().finally(() => {
			this.#loadPromise = null;
		});
		return this.#loadPromise;
	}

	async #fetch(): Promise<void> {
		try {
			await ws.connect();
			const response = await ws.emitAndWait(ClientAction.MetadataRequest);
			if (!response.ok) {
				storeToast.error(response.message);
				return;
			}

			const parsed = z.array(RuleDefinitionSchema).safeParse(response.get("available_rules"));
			if (!parsed.success) {
				console.error("[catalog] Malformed metadata payload:", parsed.error.issues);
				return;
			}
			this.rules = parsed.data;
		} catch (error) {
			console.error("[catalog] Metadata fetch failed:", error);
		}
	}
}

export const storeCatalog = new StoreCatalog();
