/**
 * @file cardDetail.svelte.ts
 * @brief Reactive store behind the inspect-a-card popover.
 *
 * Cards live on the WebGL canvas, so they have no DOM node for the usual
 * `<Tooltip>` wrapper to anchor to. AllCards3D reports whichever face-up card
 * the pointer hit (right-click, or a touch long-press) together with the
 * pointer's viewport coordinates here; CardDetailPopover — a plain DOM sibling
 * above the canvas — renders the details.
 */

export interface CardDetailCard {
	type: string;
	value: string;
}

export interface CardDetailTarget {
	card: CardDetailCard;
	x: number;
	y: number;
}

class StoreCardDetail {
	current = $state<CardDetailTarget | null>(null);

	/**
	 * Set while a touch long-press has opened the popover, so the gesture that
	 * would otherwise fire on release (hand tap-to-select/play, discard confirm)
	 * can swallow it instead. Not reactive: it is read and cleared within one
	 * gesture, never rendered.
	 */
	#longPressFired = false;

	open(card: CardDetailCard, x: number, y: number): void {
		this.current = { card: { type: card.type, value: card.value }, x, y };
	}

	close(): void {
		if (this.current !== null) this.current = null;
	}

	/** Marks that the current gesture was a long-press, not a tap. */
	markLongPress(): void {
		this.#longPressFired = true;
	}

	/** Clears the long-press mark at the start of a fresh press. */
	resetLongPress(): void {
		this.#longPressFired = false;
	}

	/** Returns whether this gesture was a long-press and clears the mark. */
	consumeLongPress(): boolean {
		const fired = this.#longPressFired;
		this.#longPressFired = false;
		return fired;
	}
}

export const storeCardDetail = new StoreCardDetail();
