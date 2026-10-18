/**
 * @file menu.svelte.ts
 * @brief Open state of the Menu tile grid, shared by the nav Menu button,
 * the grid itself and the `menu` keyboard accelerator.
 */

class StoreMenu {
	#open = $state(false);

	get isOpen(): boolean {
		return this.#open;
	}

	open(): void {
		this.#open = true;
	}

	close(): void {
		this.#open = false;
	}

	toggle(): void {
		this.#open = !this.#open;
	}
}

export const storeMenu = new StoreMenu();
