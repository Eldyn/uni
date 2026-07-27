/**
 * @file pointerMode.svelte.ts
 * @brief Whether the device can hover — the one thing that decides how playing
 * a card works.
 *
 * With a mouse you can preview a card by pointing at it, so a click can commit
 * straight away. With a finger there is no preview, and a mis-play in a card
 * game is unrecoverable, hence the two-step pick-then-confirm gesture. This is
 * a capability question, not a size one: a narrow desktop window still has a
 * mouse, and a large tablet still has none.
 */

export const HOVER_MEDIA_QUERY = "(hover: hover) and (pointer: fine)";

class PointerMode {
	/** True on mouse/trackpad devices, false on touch. */
	canHover = $state(false);
	#forced = false;

	constructor() {
		if (typeof window === "undefined" || !window.matchMedia) return;

		const query = window.matchMedia(HOVER_MEDIA_QUERY);
		this.canHover = query.matches;
		// Plugging in a mouse, or dragging the window to a touchscreen, flips it
		// live — no reload, and no stale gesture model left armed.
		query.addEventListener("change", (event) => {
			if (!this.#forced) this.canHover = event.matches;
		});
	}

	/**
	 * Pins the gesture model regardless of what the device reports. Only the
	 * local screenshot harness uses this — a desktop browser can otherwise never
	 * be made to show the touch flow, which is exactly the flow worth capturing.
	 */
	force(canHover: boolean) {
		this.#forced = true;
		this.canHover = canHover;
	}
}

export const pointerMode = new PointerMode();
