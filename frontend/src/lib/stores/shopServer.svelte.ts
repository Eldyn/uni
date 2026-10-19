/**
 * @file shopServer.svelte.ts
 * @brief The server-backed economy the Shop screen talks to.
 * Reads the account's balance, ownership and ledger from `/economy/me` and
 * buys through `/shop/buy`, validating every payload before trusting it.
 */

import { z } from "zod";
import type { BuyFailure, BuyResult, ShopBackend } from "$lib/shop/shopBackend";

/** Lifecycle of the account economy: nothing fetched, in flight, loaded, failed. */
export type ShopStatus = "idle" | "loading" | "ready" | "error";

/** One row of the account's cash ledger, newest first as the server sends it. */
export interface LedgerRow {
	amount: number;
	source: string;
	ref: string;
	created_at: number;
}

/** Wire shape of `GET /economy/me`. Loose, so the server may add fields. */
const EconomyMeSchema = z.looseObject({
	balance: z.number().int().nonnegative(),
	owned: z.array(z.string()),
	ledger: z.array(
		z.looseObject({
			amount: z.number().int(),
			source: z.string(),
			ref: z.string(),
			created_at: z.number().int()
		})
	)
});

/** Wire shape of a successful `POST /shop/buy`. */
const BuyResponseSchema = z.looseObject({
	balance: z.number().int().nonnegative(),
	item: z.string()
});

/** The economy endpoints, relative to the app origin. */
const ECONOMY_ME_URL = "/economy/me";
const BUY_URL = "/shop/buy";

/** The failure reason each rejected purchase status stands for. */
const BUY_FAILURE_BY_STATUS: Readonly<Record<number, BuyFailure>> = {
	402: "insufficient_cash",
	409: "already_owned",
	410: "not_available",
	404: "unknown_item",
	403: "guest"
};

/**
 * @class ShopServer
 * @brief Holds the account's balance and ownership, synced with the server.
 *
 * A buy keeps its request id until the server gives a definite answer, so a
 * retry after a network failure reuses the same id and the server's idempotent
 * purchase cannot charge twice. Two calls for the same item while one is in
 * flight share that request.
 */
export class ShopServer implements ShopBackend {
	/** The account's cash balance, 0 until the first successful fetch. */
	balance = $state(0);

	/** Where the account economy currently is in its request lifecycle. */
	status = $state<ShopStatus>("idle");

	/** True when `/economy/me` rejected the session, so the screen can send the user to log in. */
	needsLogin = $state(false);

	/** The account's recent ledger rows, newest first. */
	ledger = $state<readonly LedgerRow[]>([]);

	#owned = $state<Set<string>>(new Set());

	/** Buys currently in flight, keyed by item id. */
	#inFlight = new Map<string, Promise<BuyResult>>();

	/** The request id a retry of the last click must reuse, keyed by item id. */
	#requestIds = new Map<string, string>();

	/** The ids the account owns, starters included. */
	get owned(): ReadonlySet<string> {
		return this.#owned;
	}

	/**
	 * @brief Fetches the account's balance, ownership and ledger.
	 * A 401 marks the session as expired (`needsLogin`); a 403 is a guest, who
	 * has no account economy and reads as an empty balance. Any other failure,
	 * including a malformed body, leaves the previous state and sets `error`.
	 */
	async fetchMine(): Promise<void> {
		this.status = "loading";
		this.needsLogin = false;
		try {
			const res = await fetch(ECONOMY_ME_URL, {
				credentials: "include",
				headers: { "Content-Type": "application/json" }
			});

			if (res.status === 401) {
				this.status = "error";
				this.needsLogin = true;
				return;
			}

			if (res.status === 403) {
				this.balance = 0;
				this.#owned = new Set();
				this.ledger = [];
				this.status = "ready";
				return;
			}

			if (!res.ok) {
				this.status = "error";
				return;
			}

			const data = await res.json().catch(() => null);
			const parsed = EconomyMeSchema.safeParse(data);
			if (!parsed.success) {
				this.status = "error";
				return;
			}

			this.balance = parsed.data.balance;
			this.#owned = new Set(parsed.data.owned);
			this.ledger = parsed.data.ledger;
			this.status = "ready";
		} catch {
			this.status = "error";
		}
	}

	/**
	 * @brief Attempts to buy an item through the server.
	 * Sends one request per item while a buy is in flight; a retry after a
	 * network failure reuses the request id of the click it belongs to.
	 * @param itemId The catalogue id to buy.
	 */
	buy(itemId: string): Promise<BuyResult> {
		const inFlight = this.#inFlight.get(itemId);
		if (inFlight) return inFlight;

		const requestId = this.#requestIds.get(itemId) ?? crypto.randomUUID();
		this.#requestIds.set(itemId, requestId);

		const pending = this.#sendBuy(itemId, requestId).finally(() => {
			this.#inFlight.delete(itemId);
		});
		this.#inFlight.set(itemId, pending);
		return pending;
	}

	/**
	 * @brief Performs one purchase request and folds a good answer into state.
	 * A malformed 200 body is treated like a network failure: nothing is
	 * trusted and the request id stays for the retry.
	 */
	async #sendBuy(itemId: string, requestId: string): Promise<BuyResult> {
		try {
			const res = await fetch(BUY_URL, {
				method: "POST",
				credentials: "include",
				headers: { "Content-Type": "application/json" },
				body: JSON.stringify({ item: itemId, request_id: requestId })
			});

			if (res.ok) {
				const data = await res.json().catch(() => null);
				const parsed = BuyResponseSchema.safeParse(data);
				if (!parsed.success || parsed.data.item !== itemId) {
					return { ok: false, reason: "network" };
				}

				this.#requestIds.delete(itemId);
				this.balance = parsed.data.balance;
				this.#owned.add(parsed.data.item);
				return { ok: true, balance: parsed.data.balance };
			}

			const failure = BUY_FAILURE_BY_STATUS[res.status];
			if (failure !== undefined) {
				this.#requestIds.delete(itemId);
				return { ok: false, reason: failure };
			}

			if (res.status === 401) {
				this.#requestIds.delete(itemId);
				this.status = "error";
				this.needsLogin = true;
			}
			return { ok: false, reason: "network" };
		} catch {
			return { ok: false, reason: "network" };
		}
	}
}

/** Singleton instance exported for use by the Svelte components. */
export const shopServer = new ShopServer();
