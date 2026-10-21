// The marketplace secondary sidebar: a Discover group of curated shelves and a
// categories group, with the active section marked as the current page.

import { afterEach, describe, expect, it, vi } from "vitest";
import { cleanup, fireEvent, render, screen, within } from "@testing-library/svelte";
import ShopSidebar from "$components/shop/ShopSidebar.svelte";

const LINKS = [
	{ id: "featured", label: "Featured", group: "discover" },
	{ id: "popular", label: "Popular", group: "discover" },
	{ id: "new", label: "New", group: "discover" },
	{ id: "wishlist", label: "Wishlist", group: "discover" },
	{ id: "character", label: "Character", group: "categories" },
	{ id: "table", label: "Table", group: "categories" },
	{ id: "collections", label: "Collections", group: "categories" }
] as const;

function renderSidebar(active: string = "featured") {
	return render(ShopSidebar, {
		props: {
			active,
			onselect: vi.fn(),
			discoverLabel: "Discover",
			categoriesLabel: "Shop categories",
			links: LINKS
		}
	});
}

afterEach(cleanup);

describe("ShopSidebar", () => {
	it("renders the Discover group and the categories group", () => {
		const { container } = renderSidebar();

		const groups = container.querySelectorAll('[role="group"]');
		expect(groups).toHaveLength(2);
		expect(groups[0]).toHaveTextContent("Discover");
		expect(within(groups[0] as HTMLElement).getAllByRole("button")).toHaveLength(4);
		expect(within(groups[1] as HTMLElement).getAllByRole("button")).toHaveLength(3);
	});

	it("renders every section link in order", () => {
		renderSidebar();

		expect(screen.getAllByRole("button").map((button) => button.textContent?.trim())).toEqual([
			"Featured",
			"Popular",
			"New",
			"Wishlist",
			"Character",
			"Table",
			"Collections"
		]);
	});

	it("marks only the active section as the current page", () => {
		renderSidebar("table");

		expect(screen.getByRole("button", { name: "Table" })).toHaveAttribute("aria-current", "page");
		expect(screen.getByRole("button", { name: "Featured" })).not.toHaveAttribute("aria-current");
	});

	it("reports the clicked section", async () => {
		const onselect = vi.fn();
		render(ShopSidebar, {
			props: {
				active: "featured",
				onselect,
				discoverLabel: "Discover",
				categoriesLabel: "Shop categories",
				links: LINKS
			}
		});

		await fireEvent.click(screen.getByRole("button", { name: "Wishlist" }));
		expect(onselect).toHaveBeenCalledWith("wishlist");
	});
});
