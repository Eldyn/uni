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
