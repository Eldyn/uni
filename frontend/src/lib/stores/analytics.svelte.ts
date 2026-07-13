/**
 * @file analytics.svelte.ts
 * @brief Thin, privacy-conscious wrapper around Google Analytics (gtag.js).
 *
 * The gtag.js script and GA4 config are loaded in `index.html`. This store is
 * the single place the app emits custom events, so every call site stays a
 * one-liner and the `window.gtag` guard lives in exactly one location.
 *
 * Transparency: the events sent here describe gameplay only, which screens are
 * visited, lobby/match lifecycle, the rules a match was started with, and
 * coarse error/disconnect signals. No message content, card values, chat, or
 * personal data is collected. This data is used ONLY for game research
 * (understanding which rules and flows players use) to guide development.
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
 * @tag FRONT-ANALYTICS-001
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
