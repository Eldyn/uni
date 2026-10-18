import { describe, it, expect, vi, beforeEach } from "vitest";
import { render, screen, fireEvent } from "@testing-library/svelte";
import { tick } from "svelte";

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
import { storeMenu } from "$stores/menu.svelte";
import { storeNavigation } from "$lib/stores/navigation.svelte";

beforeEach(() => {
	vi.clearAllMocks();
	storeMenu.close();
	storeNavigation.current = "main";
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
		expect(screen.getByRole("button", { name: /menu/i })).toBeInTheDocument();
		expect(screen.getAllByRole("link")).toHaveLength(4);
	});

	it("menu item is a button that opens the grid", async () => {
		render(NavBar);
		const menuButton = screen.getByRole("button", { name: /menu/i });

		expect(menuButton).toHaveAttribute("aria-haspopup", "menu");
		expect(menuButton).toHaveAttribute("aria-expanded", "false");
		expect(screen.queryByRole("menu")).not.toBeInTheDocument();

		await fireEvent.click(menuButton);
		await tick();

		expect(menuButton).toHaveAttribute("aria-expanded", "true");
		expect(screen.getByRole("menu")).toBeInTheDocument();
	});

	it("menu item has aria-current page on settings and stats", async () => {
		storeNavigation.current = "settings";
		const { unmount } = render(NavBar);
		expect(screen.getByRole("button", { name: /menu/i })).toHaveAttribute("aria-current", "page");
		unmount();

		storeNavigation.current = "stats";
		render(NavBar);
		expect(screen.getByRole("button", { name: /menu/i })).toHaveAttribute("aria-current", "page");
	});

	it("menu item has no aria-current outside settings and stats", () => {
		storeNavigation.current = "main";
		render(NavBar);
		expect(screen.getByRole("button", { name: /menu/i })).not.toHaveAttribute("aria-current");
	});

	it("escape closes the grid and focuses the menu button", async () => {
		render(NavBar);
		const menuButton = screen.getByRole("button", { name: /menu/i });

		await fireEvent.click(menuButton);
		await tick();
		expect(storeMenu.isOpen).toBe(true);

		await fireEvent.keyDown(screen.getByRole("menu"), { key: "Escape" });
		await tick();

		expect(storeMenu.isOpen).toBe(false);
		expect(screen.queryByRole("menu")).not.toBeInTheDocument();
		expect(menuButton).toHaveFocus();
	});

	it("a click outside closes the grid", async () => {
		render(NavBar);
		const menuButton = screen.getByRole("button", { name: /menu/i });

		await fireEvent.click(menuButton);
		await tick();
		expect(storeMenu.isOpen).toBe(true);

		await fireEvent.click(document.body);
		await tick();

		expect(storeMenu.isOpen).toBe(false);
	});

	it("marks the active destination with aria-current", async () => {
		storeNavigation.current = "lobbies";
		render(NavBar);

		expect(screen.getByRole("link", { name: /browse/i })).toHaveAttribute("aria-current", "page");
		expect(screen.getByRole("link", { name: /home/i })).not.toHaveAttribute("aria-current");
	});

	it("assigns per-item --nav-item-color style for active/hover state", () => {
		render(NavBar);
		const homeLink = screen.getByRole("link", { name: /home/i });
		expect(homeLink.getAttribute("style")).toContain("--nav-item-color");
	});
});
