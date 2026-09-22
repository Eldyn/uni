/**
 * @file autofocus.ts
 * @brief Moves DOM focus onto a prompt's control when the prompt appears.
 *
 * A plain Svelte action runs once on mount, which is enough for a prompt whose
 * button mounts fresh. But the same prompt can stay mounted while its contents
 * change (e.g. "You drew a playable card!" for the next drawn card), and
 * Svelte reuses the element instead of remounting it. `update` re-focuses
 * whenever `enabled` flips false -> true or `key` changes, so the newest prompt
 * always owns the keyboard. `enabled = false` leaves focus alone, which is what
 * a rendered-but-inactive prompt needs.
 */
export interface AutofocusOptions {
	/** Focus only while true. Defaults to true. */
	enabled?: boolean;
	/** Re-focus when this identity changes (e.g. the drawn card's id). */
	key?: unknown;
}

/**
 * @brief Svelte action: focus `node` when the prompt is (re)shown.
 * @param node The control to focus.
 * @param options A boolean enable flag, or `{ enabled, key }`.
 */
export function autofocus(node: HTMLElement, options: AutofocusOptions | boolean = true) {
	let enabled = typeof options === "boolean" ? options : (options.enabled ?? true);
	let key = typeof options === "boolean" ? undefined : options.key;
	let frame = 0;

	function focus() {
		cancelAnimationFrame(frame);
		frame = requestAnimationFrame(() => {
			if (!enabled || !node.isConnected) return;
			node.focus({ preventScroll: true });
		});
	}

	if (enabled) focus();

	return {
		update(next: AutofocusOptions | boolean) {
			const wasEnabled = enabled;
			enabled = typeof next === "boolean" ? next : (next.enabled ?? true);
			const nextKey = typeof next === "boolean" ? undefined : next.key;
			const keyChanged = nextKey !== undefined && nextKey !== key;
			key = nextKey;
			if (enabled && (!wasEnabled || keyChanged)) focus();
		},
		destroy() {
			cancelAnimationFrame(frame);
		}
	};
}
