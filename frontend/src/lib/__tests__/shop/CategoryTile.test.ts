// A large marketplace category tile: the art fills the tile over a pixel-bordered
// surface and the title stays in Pypx thick. Pressing it selects the category.

import { afterEach, describe, expect, it, vi } from "vitest";
import { cleanup, fireEvent, render, screen } from "@testing-library/svelte";
import CategoryTile from "$components/shop/CategoryTile.svelte";

afterEach(cleanup);

describe("CategoryTile", () => {
	it("shows the category label on a large tile", () => {
		const { container } = render(CategoryTile, {
			props: {
				category: "character",
				label: "Character",
				art: "/assets/base_player_strip.png",
				sprite: true,
				onselect: vi.fn()
			}
		});

		const tile = container.querySelector(".category-tile");
		expect(tile?.getAttribute("data-category")).toBe("character");
		expect(tile).toHaveTextContent("Character");
		expect(container.querySelector(".avatar-sprite")).toBeTruthy();
	});

	it("draws still art as an image", () => {
		const { container } = render(CategoryTile, {
			props: {
				category: "table",
				label: "Table",
				art: "/assets/playmat.png",
				onselect: vi.fn()
			}
		});

		expect(container.querySelector("img")?.getAttribute("src")).toContain("/assets/playmat.png");
	});

	it("reports the pressed category", async () => {
		const onselect = vi.fn();
		render(CategoryTile, {
			props: { category: "collections", label: "Collections", onselect }
		});

		await fireEvent.click(screen.getByRole("button", { name: "Collections" }));
		expect(onselect).toHaveBeenCalledTimes(1);
	});
});
