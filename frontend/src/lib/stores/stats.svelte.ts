/**
 * @file stats.svelte.ts
 * @brief Reactive global store for managing statistics and the global leaderboard.
 * Interfaces with the REST backend to retrieve the user's data and the Top 50.
 */

import type { SessionStore } from "$stores/sessionStore";
import { storeToast } from "./toast.svelte";
import { storeI18n } from "./i18n.svelte";
import * as m from "$lib/paraglide/messages.js";

/**
 * @interface PlayerStats
 * @brief Represents the complete statistics of a single player returned by the server.
 * Includes wins, losses and an analytical breakdown of the individual cards played (if available).
 */
export interface PlayerStats {
	/** The unique username of the player. */
	username: string;
	/** Total number of matches won. */
	total_wins: number;
	/** Total number of matches lost. */
	total_losses: number;
	/** Position in the global leaderboard (null if the player is not yet ranked). */
	rank: number | null;

	/** Counter of red cards played (optional). */
	cards_played_red?: number;
	/** Counter of blue cards played (optional). */
	cards_played_blue?: number;
	/** Counter of green cards played (optional). */
	cards_played_green?: number;
	/** Counter of yellow cards played (optional). */
	cards_played_yellow?: number;
	/** Counter of '0' cards played (optional). */
	cards_played_0?: number;
	/** Counter of '1' cards played (optional). */
	cards_played_1?: number;
	/** Counter of '2' cards played (optional). */
	cards_played_2?: number;
	/** Counter of '3' cards played (optional). */
	cards_played_3?: number;
	/** Counter of '4' cards played (optional). */
	cards_played_4?: number;
	/** Counter of '5' cards played (optional). */
	cards_played_5?: number;
	/** Counter of '6' cards played (optional). */
	cards_played_6?: number;
	/** Counter of '7' cards played (optional). */
	cards_played_7?: number;
	/** Counter of '8' cards played (optional). */
	cards_played_8?: number;
	/** Counter of '9' cards played (optional). */
	cards_played_9?: number;
	/** Counter of 'Skip' cards played (optional). */
	cards_played_skip?: number;
	/** Counter of 'Reverse' cards played (optional). */
	cards_played_reverse?: number;
	/** Counter of '+2' cards played (optional). */
	cards_played_draw2?: number;
	/** Counter of '+4' cards played (optional). */
	cards_played_draw4?: number;
	/** Counter of 'Jolly' (colour change) cards played (optional). */
	cards_played_jolly?: number;
}

/**
 * @class StoreStats
 * @brief Encapsulates the state and methods to retrieve statistics from the server.
 */
class StoreStats implements SessionStore {
	/** Reactive state containing the personal statistics of the logged-in user. */
	myStats = $state<PlayerStats | null>(null);

	/** Reactive state containing the leaderboard of the top 50 players. */
	leaderboard = $state<PlayerStats[]>([]);

	/** Boolean flag indicating whether a network request is in progress, useful for UI spinners. */
	isLoading = $state(false);

	/**
	 * @brief Retrieves the personal statistics from the server.
	 * Makes a GET request to `/stats/me` including the credentials (cookie).
	 * On success, decodes the JSON and populates the `myStats` state.
	 * @returns Empty promise resolved when the network operation completes.
	 */
	async fetchMe(): Promise<void> {
		try {
			const res = await fetch("/stats/me", {
				credentials: "include",
				headers: { "Content-Type": "application/json" }
			});

			if (res.ok) {
				this.myStats = await res.json();
			} else {
				storeToast.error(m.stats_toast_load_failed({}, { locale: storeI18n.locale }));
			}
		} catch {
			storeToast.error(m.stats_toast_load_failed({}, { locale: storeI18n.locale }));
		}
	}

	/**
	 * @brief Retrieves the global leaderboard from the server.
	 * Makes a GET request to `/stats/leaderboard` and updates the `leaderboard` state array
	 * to let the UI render the table. Automatically manages the loading state.
	 * @returns Empty promise resolved when the operation completes.
	 */
	async fetchLeaderboard(): Promise<void> {
		this.isLoading = true;
		try {
			const res = await fetch("/stats/leaderboard", {
				credentials: "include",
				headers: { "Content-Type": "application/json" }
			});

			if (res.ok) {
				const data = await res.json();
				this.leaderboard = data.leaderboard || [];
			} else {
				storeToast.error(m.stats_toast_leaderboard_failed({}, { locale: storeI18n.locale }));
			}
		} catch {
			storeToast.error(
				m.stats_toast_leaderboard_network_error({}, { locale: storeI18n.locale })
			);
		} finally {
			this.isLoading = false;
		}
	}

	/** @brief Clears the previous account's stats and cached leaderboard. */
	reset(): void {
		this.myStats = null;
		this.leaderboard = [];
		this.isLoading = false;
	}
}

/**
 * @brief Singleton instance exported for use by the Svelte components.
 */
export const storeStats = new StoreStats();
