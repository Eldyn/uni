/**
 * @file modal.svelte.ts
 * @brief Tracks how many Modal.svelte instances are currently open, so
 * app-wide logic (the keyboard accelerators in keyboardAccelerators.ts) can
 * ask "is any modal open" without each modal wiring up its own signal.
 */
class StoreModal {
	#openCount = $state(0);

	get isAnyOpen(): boolean {
		return this.#openCount > 0;
	}

	register(): void {
		this.#openCount += 1;
	}

	unregister(): void {
		this.#openCount = Math.max(0, this.#openCount - 1);
	}
}

export const storeModal = new StoreModal();
