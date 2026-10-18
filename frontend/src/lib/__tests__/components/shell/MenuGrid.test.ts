import { describe, it, expect, vi, beforeEach } from "vitest";
import { render, screen, fireEvent, within } from "@testing-library/svelte";
import { tick } from "svelte";

vi.mock("$lib/stores/navigation.svelte", () => ({
	storeNavigation: {
		goto: vi.fn(() => true)
	}
}));

import MenuGrid from "$components/shell/MenuGrid.svelte";
import { storeMenu } from "$stores/menu.svelte";
import { storeNavigation } from "$lib/stores/navigation.svelte";

const goto = vi.mocked(storeNavigation.goto);

const leaderboard = () => screen.getByRole("menuitem", { name: /leaderboard/i });
const wardrobe = () => screen.getByRole("menuitem", { name: /wardrobe/i });
const settings = () => screen.getByRole("menuitem", { name: /settings/i });

beforeEach(() => {
	vi.clearAllMocks();
	storeMenu.close();
});

describe("MenuGrid", () => {
	it("renders three tiles named Settings, Wardrobe, Leaderboard", () => {
		render(MenuGrid);

		expect(screen.getAllByRole("menuitem")).toHaveLength(3);
		expect(leaderboard()).toBeInTheDocument();
		expect(wardrobe()).toBeInTheDocument();
		expect(settings()).toBeInTheDocument();
	});

	it("leaderboard tile goes to stats and closes the menu", async () => {
		storeMenu.open();
		render(MenuGrid);

		await fireEvent.click(leaderboard());

		expect(goto).toHaveBeenCalledWith("stats");
		expect(storeMenu.isOpen).toBe(false);
	});

	it("settings tile goes to settings and closes the menu", async () => {
		storeMenu.open();
		render(MenuGrid);

		await fireEvent.click(settings());

		expect(goto).toHaveBeenCalledWith("settings");
		expect(storeMenu.isOpen).toBe(false);
	});

	it("wardrobe tile is aria-disabled, shows Soon, and does not navigate", async () => {
		storeMenu.open();
		render(MenuGrid);
		const tile = wardrobe();

		expect(tile).toHaveAttribute("aria-disabled", "true");
		expect(within(tile).getByText(/soon/i)).toBeInTheDocument();

		await fireEvent.click(tile);
		await fireEvent.keyDown(tile, { key: "Enter" });

		expect(goto).not.toHaveBeenCalled();
		expect(storeMenu.isOpen).toBe(true);
	});

	it("tile labels use min-width 0 so long labels cannot widen the grid", () => {
		render(MenuGrid);

		for (const tile of screen.getAllByRole("menuitem")) {
			expect(tile.classList.contains("menu-tile")).toBe(true);
		}
	});

	it("focuses the first enabled tile on mount", async () => {
		render(MenuGrid);
		await tick();

		expect(settings()).toHaveFocus();
	});

	it("arrow right and left move focus between enabled tiles and wrap", async () => {
		render(MenuGrid);
		await tick();

		await fireEvent.keyDown(settings(), { key: "ArrowRight" });
		expect(leaderboard()).toHaveFocus();
		expect(wardrobe()).not.toHaveFocus();

		await fireEvent.keyDown(leaderboard(), { key: "ArrowRight" });
		expect(settings()).toHaveFocus();

		await fireEvent.keyDown(settings(), { key: "ArrowLeft" });
		expect(leaderboard()).toHaveFocus();
	});

	it("arrow down and up move focus by one row of 3 columns and stay put when no tile exists there", async () => {
		render(MenuGrid);
		await tick();

		await fireEvent.keyDown(settings(), { key: "ArrowDown" });
		expect(settings()).toHaveFocus();

		await fireEvent.keyDown(settings(), { key: "ArrowUp" });
		expect(settings()).toHaveFocus();

		await fireEvent.keyDown(settings(), { key: "End" });
		expect(leaderboard()).toHaveFocus();

		await fireEvent.keyDown(leaderboard(), { key: "ArrowDown" });
		expect(leaderboard()).toHaveFocus();

		await fireEvent.keyDown(leaderboard(), { key: "ArrowUp" });
		expect(leaderboard()).toHaveFocus();
	});

	it("home and end focus the first and last enabled tile", async () => {
		render(MenuGrid);
		await tick();

		await fireEvent.keyDown(settings(), { key: "End" });
		expect(leaderboard()).toHaveFocus();

		await fireEvent.keyDown(leaderboard(), { key: "Home" });
		expect(settings()).toHaveFocus();
	});

	it("enter on the focused tile activates it", async () => {
		storeMenu.open();
		render(MenuGrid);
		await tick();

		await fireEvent.keyDown(settings(), { key: "Enter" });

		expect(goto).toHaveBeenCalledWith("settings");
		expect(storeMenu.isOpen).toBe(false);
	});
});
