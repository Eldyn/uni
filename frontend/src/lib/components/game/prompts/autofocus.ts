/**
 * @file autofocus.ts
 * @brief Svelte action that focuses a prompt control once it mounts, so the
 * first option of a freshly opened prompt is keyboard-reachable immediately.
 */

export function autofocus(node: HTMLElement, enabled = true) {
	if (enabled) {
		requestAnimationFrame(() => {
			node.focus();
		});
	}
}
