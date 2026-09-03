import { describe, it, expect, vi } from "vitest";
import { render, screen } from "@testing-library/svelte";

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

describe("LobbyBrowse create action placement", () => {
	it("gives the list a bottom padding class so a floating action button cannot bury the last card", () => {
		const { container } = render(LobbyBrowse);
		const list = container.querySelector("[data-testid='lobby-list']");
		expect(list, "expected the list container to carry data-testid='lobby-list'").toBeTruthy();
		expect(list!.className).toMatch(/pb-(20|24|28|32)/);
	});
});
