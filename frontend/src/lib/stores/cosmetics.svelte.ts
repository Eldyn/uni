/**
 * @file cosmetics.svelte.ts
 * @brief Reactive store for the local account's equipped cosmetics.
 * Fetches the slots from `/cosmetics/me` and equips through the shop backend,
 * keeping the last good state when a request fails.
 */

import type { CosmeticSlot, CosmeticSlots } from "$lib/shop/shopBackend";
import { shopServer } from "$stores/shopServer.svelte";
import type { SessionStore } from "$stores/sessionStore";

/** Lifecycle of the local cosmetics: nothing fetched, in flight, loaded, failed. */
export type CosmeticsStatus = "idle" | "loading" | "ready" | "error";

/** Result of an equip: ok, or the server's rejection code. */
export type EquipResult = { ok: true } | { ok: false; code: string };

/** Every slot empty, the default look. */
const EMPTY_SLOTS: CosmeticSlots = { card_face: null, card_back: null, playmat: null };

/**
 * @class StoreCosmetics
 * @brief Holds the local account's equipped cosmetics and syncs them with the server.
 */
class StoreCosmetics implements SessionStore {
	/** Where the local cosmetics currently are in their request lifecycle. */
	status = $state<CosmeticsStatus>("idle");

	/** The equipped item id per slot, all null until fetched. */
	slots = $state<CosmeticSlots>({ ...EMPTY_SLOTS });

	/**
	 * @brief Fetches the local account's equipped cosmetics.
	 * A rejection or malformed payload leaves the previous slots and sets `error`.
	 */
	async fetch(): Promise<void> {
		this.status = "loading";

		const result = await shopServer.cosmetics();
		if (!result.ok) {
			this.status = "error";
			return;
		}

		this.slots = result.slots;
		this.status = "ready";
	}

	/**
	 * @brief Equips `item` in `slot`, optimistically.
	 * The slot switches before the request resolves and rolls back to its
	 * previous value when the server rejects it.
	 * @param slot The slot to equip into.
	 * @param item The owned cosmetic's id.
	 */
	async equip(slot: CosmeticSlot, item: string): Promise<EquipResult> {
		const previous = this.slots;
		this.slots = { ...previous, [slot]: item };

		const result = await shopServer.equipCosmetic(slot, item);
		if (!result.ok) {
			this.slots = previous;
			return { ok: false, code: result.code };
		}

		this.slots = result.slots;
		this.status = "ready";
		return { ok: true };
	}

	/** @brief Clears the previous account's cosmetics on logout. */
	reset(): void {
		this.slots = { ...EMPTY_SLOTS };
		this.status = "idle";
	}
}

/** Singleton instance exported for use by the Svelte components. */
export const storeCosmetics = new StoreCosmetics();
