import { describe, it, expect, vi } from "vitest";
import { render, screen } from "@testing-library/svelte";

// jsdom has no ResizeObserver; MainScreen's bind:clientHeight needs one to mount.
class StubResizeObserver {
	observe() {}
	unobserve() {}
	disconnect() {}
}
vi.stubGlobal("ResizeObserver", StubResizeObserver);

vi.mock("$lib/stores/auth.svelte", () => ({
	storeAuth: { isLoggedIn: false, isGuest: false, isLoading: false, username: "" }
}));
vi.mock("$lib/stores/navigation.svelte", () => ({ storeNavigation: { gotoAuth: vi.fn(), goto: vi.fn() } }));

import MainScreen from "$components/MainScreen.svelte";

describe("MainScreen keyboard reachability", () => {
	it("offers a skip link into the marketing content, targeting a real anchor", () => {
		render(MainScreen);
		const skipLink = screen.getByRole("link", { name: /more about uni/i });
		const href = skipLink.getAttribute("href");
		expect(href).toMatch(/^#/);
		const target = document.querySelector(href!);
		expect(target).not.toBeNull();
	});
});
