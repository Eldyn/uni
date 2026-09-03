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
vi.mock("$lib/stores/catalog.svelte", () => ({ storeCatalog: { ensureLoaded: vi.fn(), rules: [] } }));
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

describe("LobbyBrowse create action placement", () => {
	it("renders a Create FAB and reserves bottom padding on a portrait phone", () => {
		setViewport(390, 844);
		const { container, getByLabelText } = render(LobbyBrowse);

		expect(getByLabelText("Create lobby")).toBeInTheDocument();

		const list = container.querySelector("[data-testid='lobby-list']");
		expect(list, "expected the list container to carry data-testid='lobby-list'").toBeTruthy();
		expect(list!.className).toMatch(/pb-(20|24|28|32|36|40)/);
	});

	it("does not render the FAB or reserve its padding on desktop", () => {
		setViewport(1440, 900);
		const { container, queryByLabelText } = render(LobbyBrowse);

		expect(queryByLabelText("Create lobby")).not.toBeInTheDocument();

		const list = container.querySelector("[data-testid='lobby-list']");
		expect(list, "expected the list container to carry data-testid='lobby-list'").toBeTruthy();
		expect(list!.className).not.toMatch(/pb-(20|24|28|32|36|40)/);
	});
});
