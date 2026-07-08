/**
 * @file clickOutside.ts
 * @brief `use:clickOutside={callback}` — fires the callback when a click
 * lands outside the node. Capture phase so stopPropagation inside other
 * widgets can't swallow the dismissal.
 */

export function clickOutside(node: HTMLElement, callback: () => void) {
	let cb = callback;

	const handle = (e: MouseEvent) => {
		if (!node.contains(e.target as Node)) cb();
	};
	document.addEventListener("click", handle, true);

	return {
		update(newCallback: () => void) {
			cb = newCallback;
		},
		destroy() {
			document.removeEventListener("click", handle, true);
		}
	};
}
