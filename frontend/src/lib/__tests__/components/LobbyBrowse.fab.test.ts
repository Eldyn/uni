import { describe, it, expect, vi, afterEach } from "vitest";
import { render } from "@testing-library/svelte";

// jsdom has no ResizeObserver; LobbyBrowse's bind:clientWidth needs one to mount.
class StubResizeObserver {
	observe() {}
	unobserve() {}
	disconnect() {}
}
vi.stubGlobal("ResizeObserver", StubResizeObserver);

vi.mock("$lib/stores/lobby.svelte", () => ({
	storeLobby: { available: [], fetchList: vi.fn(), listError: false }
}));
vi.mock("$lib/stores/catalog.svelte", () => ({
	storeCatalog: { ensureLoaded: vi.fn(), rules: [] }
}));
vi.mock("$lib/stores/auth.svelte", () => ({ storeAuth: { username: "eldyn" } }));
vi.mock("$lib/stores/navigation.svelte", () => ({ storeNavigation: { goto: vi.fn() } }));

import LobbyBrowse from "$components/lobby/LobbyBrowse.svelte";

// LobbyBrowse mirrors ShellFrame's rail-vs-bottom-nav breakpoint via
// `<svelte:window bind:innerWidth bind:innerHeight>`, so setting these
// globals before render (jsdom reads them synchronously on mount) is enough
// to drive isPortraitPhone without dispatching a resize event.
function setViewport(width: number, height: number) {
	Object.defineProperty(window, "innerWidth", { configurable: true, value: width });
	Object.defineProperty(window, "innerHeight", { configurable: true, value: height });
}

const DEFAULT_WIDTH = window.innerWidth;
const DEFAULT_HEIGHT = window.innerHeight;

afterEach(() => {
	setViewport(DEFAULT_WIDTH, DEFAULT_HEIGHT);
});

// The floating Create FAB used to sit at the same bottom-right corner as the
// ChatDock launcher on a portrait phone, hiding behind it. Create now lives
// in the toolbar next to Advanced on every viewport, so there's no separate
// FAB to hide the chat launcher's clearance padding was reserved for.
describe("LobbyBrowse create action placement", () => {
	it("renders Create in the toolbar on a portrait phone", () => {
		setViewport(390, 844);
		const { getByTitle } = render(LobbyBrowse);

		expect(getByTitle("Create lobby")).toBeInTheDocument();
	});

	it("renders Create in the toolbar on desktop", () => {
		setViewport(1440, 900);
		const { getByTitle } = render(LobbyBrowse);

		expect(getByTitle("Create lobby")).toBeInTheDocument();
	});
});
