/**
 * @file directionRing.svelte.ts
 * @brief Direction the table ring shows, plus the queue of reversals the
 * turn-advance beat handler raised and DirectionRing3D has yet to animate.
 *
 * `sign` is updated synchronously per reversal, so two reverses inside one
 * beat batch each see the direction the previous one left behind and both
 * queue a flip.
 */

import {
	detectReverse,
	directionSign,
	type DirectionSign
} from "$components/game/animation/directionRing";

class StoreDirectionRing {
	sign = $state<DirectionSign>(1);
	token = $state(0);
	settleToken = $state(0);
	#pendingFlips: DirectionSign[] = [];

	/** Sets the resting direction without animating (mount, new match). */
	set(playDirection: number): void {
		this.sign = directionSign(playDirection);
		this.#pendingFlips = [];
	}

	/** Queues a flip when the beat's direction differs from the ring's. */
	reverseTo(playDirection: number): boolean {
		if (!detectReverse(this.sign, playDirection)) return false;
		this.sign = directionSign(playDirection);
		this.#pendingFlips.push(this.sign);
		this.token += 1;
		return true;
	}

	/** Drops queued flips so the ring snaps to `sign` without a tween, as
	 *  cardRegistry.flushImmediately does for card beats (tab return). */
	settle(): void {
		this.#pendingFlips = [];
		this.settleToken += 1;
	}

	takeFlips(): DirectionSign[] {
		const flips = this.#pendingFlips;
		this.#pendingFlips = [];
		return flips;
	}

	reset(): void {
		this.set(1);
		this.token = 0;
	}
}

export const storeDirectionRing = new StoreDirectionRing();
