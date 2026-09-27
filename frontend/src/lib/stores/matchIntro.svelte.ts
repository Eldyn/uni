/**
 * @file matchIntro.svelte.ts
 * @brief Reactive store owning the match-start cinematic's board state. The
 * board reads these overrides while the deal runs; the controller drives
 * `begin()`/`end()` and sets the two pile overrides mid-flight.
 */

export type DrawPileOverride = { x: number; z: number };

class MatchIntroStore {
	/** True for the whole cinematic. */
	active = $state(false);
	/** Overrides the draw pile's rendered card count while dealing; null = use the real size. */
	drawPileCount = $state<number | null>(null);
	/** Overrides the draw pile's world X/Z; null = use placement.drawPileX/Z. */
	drawPilePos = $state<DrawPileOverride | null>(null);
	/** Hide the discard pile while the deal runs. */
	discardHidden = $state(false);
	/** Force the playmat to rebeccapurple (#663399) while the deal runs. */
	forcePurpleMat = $state(false);

	begin(): void {
		this.active = true;
		this.discardHidden = true;
		this.forcePurpleMat = true;
	}

	end(): void {
		this.active = false;
		this.drawPileCount = null;
		this.drawPilePos = null;
		this.discardHidden = false;
		this.forcePurpleMat = false;
	}
}

export const storeMatchIntro = new MatchIntroStore();
