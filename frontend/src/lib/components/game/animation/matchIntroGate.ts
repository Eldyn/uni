/**
 * @file matchIntroGate.ts
 * @brief Pure predicate gating the match-start deal cinematic.
 *
 * The loader covers the board until its minimum card cycle finishes, which is
 * later than the server's ready barrier — so `GameBoard` reads this from a
 * reactive effect that also reads `storeGameLoader.shown`, and the effect
 * re-runs to start the intro the moment the loader hides.
 */

export interface MatchIntroGateInput {
	/** A match snapshot is live. */
	hasState: boolean;
	/** The pending-intro flag and the server's ready barrier are both set. */
	introReady: boolean;
	/** The loading screen is still covering the board. */
	loaderShown: boolean;
}

export function shouldStartMatchIntro({
	hasState,
	introReady,
	loaderShown
}: MatchIntroGateInput): boolean {
	return hasState && introReady && !loaderShown;
}
