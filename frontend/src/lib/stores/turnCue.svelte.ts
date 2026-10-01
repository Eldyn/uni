/**
 * @file turnCue.svelte.ts
 * @brief Token the match-event controller bumps when the turn-advance beat
 * hands the turn to the local player; LocalSeat3D plays the pulse on change.
 */

class StoreTurnCue {
	token = $state(0);
	settleToken = $state(0);

	fire(): void {
		this.token += 1;
	}

	/** Ends a running pulse at the steady rim (tab return), as
	 *  cardRegistry.flushImmediately does for card beats. */
	settle(): void {
		this.settleToken += 1;
	}

	reset(): void {
		this.token = 0;
	}
}

export const storeTurnCue = new StoreTurnCue();
