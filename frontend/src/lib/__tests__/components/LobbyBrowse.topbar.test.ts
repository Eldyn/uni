import { describe, it, expect, vi, afterEach } from "vitest";
import { render } from "@testing-library/svelte";

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
import { storeTopbarContent } from "$lib/stores/topbarContent.svelte";

afterEach(() => {
	storeTopbarContent.current = undefined;
});

describe("LobbyBrowse topbar merge", () => {
	it("hands TopBar a search snippet instead of rendering its own header", () => {
		const { container } = render(LobbyBrowse);
		expect(container.querySelector("header")).not.toBeInTheDocument();
		expect(storeTopbarContent.current).toBeTruthy();
	});
});
