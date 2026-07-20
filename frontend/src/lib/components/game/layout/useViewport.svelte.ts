import type { Orientation, ViewportInfo } from "./seatLayout";

/**
 * Rune-based viewport tracker: current window size + orientation, kept in
 * sync via a resize listener. Must be called during component
 * initialization (same rule as `useCardBus`/`createCardBus`).
 */
export function useViewport(): ViewportInfo {
	let width = $state(window.innerWidth);
	let height = $state(window.innerHeight);

	$effect(() => {
		const update = () => {
			width = window.innerWidth;
			height = window.innerHeight;
		};
		window.addEventListener("resize", update);
		return () => window.removeEventListener("resize", update);
	});

	return {
		get width() {
			return width;
		},
		get height() {
			return height;
		},
		get orientation(): Orientation {
			return height > width ? "portrait" : "landscape";
		}
	};
}
