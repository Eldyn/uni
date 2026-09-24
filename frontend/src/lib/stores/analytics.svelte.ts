/**
 * @file analytics.svelte.ts
 * @brief Thin, privacy-conscious wrapper around Google Analytics (gtag.js).
 *
 * The gtag.js script and GA4 config are loaded in `index.html`. This store is
 * the single place the app emits custom events, so every call site stays a
 * one-liner and the `window.gtag` guard lives in exactly one location.
 *
 * Transparency: the events sent here describe gameplay only, which screens are
 * visited, lobby/match lifecycle, and coarse error/disconnect signals. No
 * message content, card values, chat, or personal data is collected. This
 * data is used ONLY for game research (understanding which rules and flows
 * players use) to guide development.
 *
 * `screen_view` carries a coarse `account_type` (registered/guest/anonymous)
 * so real accounts can be told apart from guest sessions and non-authenticated
 * traffic (crawlers), plus the active `locale` for language distribution. This
 * is a bucket, not an identifier: no GA4 User-ID or other persistent
 * cross-session identity is ever set. `locale_change` fires only when a user
 * explicitly switches language in settings ({ from, to }).
 *
 * Match lifecycle events, host-only, one per match:
 * - `match_start`: fired when a match begins (funnel/abandonment baseline).
 *   Carries `match_key` plus minimal params (player_count, mod_count,
 *   is_public) since a started match doesn't guarantee completion.
 * - `match_end`: fired only when a match reaches a real winner. Carries
 *   outcome/timing (duration_seconds, time_to_play_avg_ms, winner_is_bot) and
 *   the match-depth counters (turn_count, human_turn_count, cards_played,
 *   auto_plays, cards_drawn, longest_turn_ms).
 * - `match_saved`: fired when a match ends without a winner but its state was
 *   saved (mid-match quit with save_state on). Carries duration + depth.
 * - `match_abandoned`: fired when a match ends with neither a winner nor a
 *   save (true abort/delete). Carries duration + depth, player/human counts,
 *   and `host_disconnects` so a flaky-network abort is distinguishable.
 * - `match_settings`: fired alongside match_end/match_saved/match_abandoned,
 *   carrying the full flattened ruleset (mods, bot_count, starting_cards,
 *   etc.) as separate params rather than a JSON blob, so GA4 can graph each
 *   field.
 * - `match_key`: a random, per-match, non-identifying join key present on
 *   match_start/match_end/match_saved/match_abandoned/match_settings. GA4's UI
 *   cannot join events by a shared param, so correlating ruleset with outcome
 *   requires the BigQuery export (query `event_params.match_key`).
 * - `rematch`: fired host-only when a second or later match starts in the same
 *   lobby ({ match_index }), i.e. the same group played again.
 * - `lobby_abandoned`: fired when a player leaves a lobby before its first
 *   match ({ dwell_ms, member_count, is_host, reason? }).
 */

/** Allowed shapes for a gtag event parameter value. */
type GtagParamValue = string | number | boolean | undefined;

/** Flat bag of parameters attached to a single analytics event. */
export type GtagParams = Record<string, GtagParamValue>;

declare global {
	interface Window {
		gtag?: (command: "event", eventName: string, params?: GtagParams) => void;
	}
}

/**
 * @class StoreAnalytics
 * @brief Emits GA4 custom events, no-op when gtag is unavailable.
 */
class StoreAnalytics {
	/**
	 * @brief Sends a custom event to GA4 if the gtag script is present.
	 * Safe to call unconditionally: silently no-ops when analytics is blocked
	 * (ad blockers), still loading, or running outside the browser.
	 * @param event GA4 event name (snake_case).
	 * @param params Optional flat parameter bag describing the event.
	 */
	track(event: string, params?: GtagParams): void {
		if (typeof window === "undefined" || typeof window.gtag !== "function") return;
		window.gtag("event", event, params ?? {});
	}
}

export const storeAnalytics = new StoreAnalytics();
