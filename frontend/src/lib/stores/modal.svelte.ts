/**
 * @file modal.svelte.ts
 * @brief Tracks how many Modal.svelte instances are currently open, so
 * app-wide logic (the keyboard accelerators in keyboardAccelerators.ts) can
 * ask "is any modal open" without each modal wiring up its own signal.
 */
import { untrack } from "svelte";

class StoreModal {
	#openCount = $state(0);

	get isAnyOpen(): boolean {
		return this.#openCount > 0;
	}

	// register()/unregister() are called from inside Modal.svelte's own
	// $effect. Reading #openCount via a compound assignment (`+= 1`) inside
	// that effect would track #openCount as one of its dependencies, and the
	// write would then immediately re-trigger the same effect — an infinite
	// loop Svelte reports as effect_update_depth_exceeded. untrack() reads
	// the current value without adding that dependency.
	register(): void {
		untrack(() => {
			this.#openCount += 1;
		});
	}

	unregister(): void {
		untrack(() => {
			this.#openCount = Math.max(0, this.#openCount - 1);
		});
	}
}

export const storeModal = new StoreModal();
