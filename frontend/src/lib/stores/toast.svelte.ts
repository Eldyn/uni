/**
 * @file toast.svelte.ts
 * @brief Global store for managing pop-up notifications (Toasts).
 * Provides a typed, auto-expiring queue to send visual feedback to the user
 * from anywhere in the application (e.g. network errors or save confirmations).
 */

/**
 * @typedef ToastType
 * @brief Type of the notification, used to determine the style and icon of the toast.
 */
export type ToastType = "success" | "error" | "info" | "warning";

/**
 * @interface Toast
 * @brief Represents a single notification currently visible on screen.
 */
export interface Toast {
	/** Unique numeric identifier of the notification, used for removal. */
	id: number;
	/** Type of notification (determines the background/border colour). */
	type: ToastType;
	/** The textual message to show to the user. */
	message: string;
}

/**
 * @class StoreToast
 * @brief Manages the reactive notification queue and their temporal lifecycle.
 */
class StoreToast {
	/** Reactive array containing the notifications currently active and visible. */
	items = $state<Toast[]>([]);

	/** Internal incremental counter to assign a unique ID to each new notification. */
	#nextId = 0;

	/**
	 * @brief Adds a new notification to the queue and sets a timer for its removal.
	 * @param message The text of the notification.
	 * @param type The severity/category of the notification (Default: "info").
	 * @param durationMs Milliseconds before automatic disappearance (Default: 5000ms). If 0, the toast does not expire.
	 * @returns A callback function that, when invoked, removes the notification prematurely.
	 */
	add(message: string, type: ToastType = "info", durationMs = 5_000): () => void {
		const id = this.#nextId++;
		this.items.push({ id, type, message });

		const remove = () => this.remove(id);
		if (durationMs > 0) setTimeout(remove, durationMs);
		return remove;
	}

	/**
	 * @brief Immediately removes a notification from the array by filtering it by ID.
	 * The Svelte UI will update the list by removing the component from the DOM.
	 * @param id The numeric identifier of the notification to delete.
	 */
	remove(id: number): void {
		this.items = this.items.filter((t) => t.id !== id);
	}

	// --- Quick utility methods (Shorthands) ---

	/**
	 * @brief Shorthand to show an error message (stays visible for 6 seconds).
	 * @param msg The error message.
	 */
	error(msg: string): void {
		this.add(msg, "error", 6_000);
	}

	/**
	 * @brief Shorthand to show a success message (stays visible for 3 seconds).
	 * @param msg The success message.
	 */
	success(msg: string): void {
		this.add(msg, "success", 3_000);
	}

	/**
	 * @brief Shorthand to show an informational message (stays visible for 5 seconds).
	 * @param msg The informational message.
	 */
	info(msg: string): void {
		this.add(msg, "info", 5_000);
	}

	/**
	 * @brief Shorthand to show a warning (stays visible for 5 seconds).
	 * @param msg The warning message.
	 */
	warning(msg: string): void {
		this.add(msg, "warning", 5_000);
	}
}

/**
 * @brief Globally exported singleton instance. It is the access point for all other stores.
 */
export const storeToast = new StoreToast();
