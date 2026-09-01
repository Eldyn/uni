import { describe, it, expect, vi, beforeEach } from "vitest";
import { render, screen } from "@testing-library/svelte";

vi.mock("$lib/stores/navigation.svelte", () => {
	const state = { current: "main" as string };
	const paths: Record<string, string> = {
		main: "/",
		lobbies: "/browse",
		decks: "/decks",
		shop: "/shop"
	};
	return {
		storeNavigation: {
			get current() {
				return state.current;
			},
			set current(value: string) {
				state.current = value;
			},
			goto: vi.fn((screen: string) => {
				state.current = screen;
				return true;
			}),
			openSettings: vi.fn()
		},
		pathForScreen: (screen: string) => paths[screen] ?? `/${screen}`,
		__setCurrent: (screen: string) => {
			state.current = screen;
		}
	};
});

import NavBar from "$components/shell/NavBar.svelte";

beforeEach(() => {
	vi.clearAllMocks();
});

describe("NavBar", () => {
	it("renders a nav landmark with the five destinations", () => {
		render(NavBar);
		const nav = screen.getByRole("navigation");
		expect(nav).toBeInTheDocument();
		expect(screen.getByRole("link", { name: /home/i })).toBeInTheDocument();
		expect(screen.getByRole("link", { name: /browse/i })).toBeInTheDocument();
		expect(screen.getByRole("link", { name: /decks/i })).toBeInTheDocument();
		expect(screen.getByRole("link", { name: /shop/i })).toBeInTheDocument();
		expect(screen.getByRole("link", { name: /menu/i })).toBeInTheDocument();
	});

	it("marks the active destination with aria-current", async () => {
		const { storeNavigation } = (await import("$lib/stores/navigation.svelte")) as unknown as {
			storeNavigation: { current: string };
		};
		storeNavigation.current = "lobbies";
		render(NavBar);

		expect(screen.getByRole("link", { name: /browse/i })).toHaveAttribute("aria-current", "page");
		expect(screen.getByRole("link", { name: /home/i })).not.toHaveAttribute("aria-current");
	});
});
