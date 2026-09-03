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

describe("MainScreen logged-out landing", () => {
	it("renders the scroll-revealed marketing content below the title screen", () => {
		render(MainScreen);
		expect(screen.getByText(/what is this/i)).toBeInTheDocument();
	});

	it("keeps the title screen's Login/Guest actions present regardless of scroll state", () => {
		render(MainScreen);
		expect(screen.getByRole("button", { name: /login/i })).toBeInTheDocument();
		expect(screen.getByRole("button", { name: /play as guest/i })).toBeInTheDocument();
	});
});
