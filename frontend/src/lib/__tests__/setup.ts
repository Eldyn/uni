import "@testing-library/jest-dom";

// Mock matchMedia for jsdom (which doesn't have it by default)
Object.defineProperty(window, "matchMedia", {
	writable: true,
	value: (query: string) => ({
		matches: false,
		media: query,
		onchange: null,
		addListener: () => {},
		removeListener: () => {},
		addEventListener: () => {},
		removeEventListener: () => {},
		dispatchEvent: () => true
	})
});

// jsdom has no ResizeObserver; Svelte's bind:clientWidth/clientHeight renders
// through one, so any component using it would otherwise throw on mount.
class ResizeObserverStub {
	observe(): void {}
	unobserve(): void {}
	disconnect(): void {}
}
globalThis.ResizeObserver ??= ResizeObserverStub as unknown as typeof ResizeObserver;

// jsdom has no Web Animations API; Svelte's built-in transitions call
// element.animate() and wait for its onfinish callback.
if (typeof Element !== "undefined" && typeof Element.prototype.animate !== "function") {
	(Element.prototype as unknown as { animate: (...args: unknown[]) => Animation }).animate =
		function (keyframes: unknown, options?: number | { duration?: number }) {
			const duration = typeof options === "number" ? options : (options?.duration ?? 0);
			const animation = {
				onfinish: null,
				oncancel: null,
				playState: "running",
				currentTime: 0,
				effect: null,
				cancel() {
					animation.playState = "idle";
				},
				finish() {}
			};
			setTimeout(() => {
				animation.playState = "finished";
				animation.onfinish?.();
			}, duration);
			return animation as unknown as Animation;
		};
}
