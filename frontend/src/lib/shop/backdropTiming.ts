/**
 * @file backdropTiming.ts
 * @brief Frame pacing for the Shop and Wardrobe backdrop: a draw-rate cap and
 * the rule for when to draw one still frame and stop.
 */

export const BACKDROP_MAX_FPS = 20;
export const BACKDROP_FRAME_INTERVAL_MS = 1000 / BACKDROP_MAX_FPS;

/** Shader time of the still frame. */
export const BACKDROP_STILL_TIME_S = 12;

const STILL_MEDIA_QUERIES = ["(prefers-reduced-motion: reduce)", "(update: slow)"];

export function shouldDrawFrame(nowMs: number, lastDrawMs: number): boolean {
	return nowMs - lastDrawMs >= BACKDROP_FRAME_INTERVAL_MS;
}

/** True when the backdrop must draw one frame at the still time and stop. */
export function prefersStillBackdrop(): boolean {
	if (typeof window === "undefined" || typeof window.matchMedia !== "function") return false;
	return STILL_MEDIA_QUERIES.some((query) => window.matchMedia(query).matches);
}
