import { afterEach, describe, expect, it, vi } from "vitest";
import { cleanup, fireEvent, render, screen } from "@testing-library/svelte";
import ItemCard, { itemLabelKey, shopItemText } from "$components/shop/ItemCard.svelte";
import type { CatalogueItem } from "$lib/avatar/types";

function makeItem(overrides: Partial<CatalogueItem> = {}): CatalogueItem {
	return {
		id: "uni:test",
		slot: "eyes",
		name: "Test Eyes",
		asset: { kind: "strip", path: "eyes/uni_eyes_dot.png" },
		regions: 1,
		rarity: "common",
		price: 250,
		...overrides
	};
}

function renderCard(item: CatalogueItem, props: Record<string, unknown> = {}) {
	const onselect = vi.fn();
	const utils = render(ItemCard, {
		props: { item, owned: false, selected: false, onselect, ...props }
	});
	return { onselect, ...utils };
}

afterEach(cleanup);

describe("itemLabelKey", () => {
	it("maps an item id to its shop_item_ message key", () => {
		expect(itemLabelKey("uni:eyes_dot")).toBe("shop_item_uni_eyes_dot");
	});
});

describe("shopItemText", () => {
	it("reads an existing message through paraglide", () => {
		expect(shopItemText("shop_title", "fallback", "en")).toBe("Shop");
	});

	it("falls back to the English default when the key is missing", () => {
		expect(shopItemText("shop_item_uni_missing", "fallback", "en")).toBe("fallback");
	});
});

describe("ItemCard", () => {
	it("falls back to the catalogue id when the shop_item_ message is missing", () => {
		renderCard(makeItem());
		expect(screen.getByText("uni:test")).toBeInTheDocument();
	});

	it("shows the price for an unowned item and no OWNED ribbon", () => {
		renderCard(makeItem({ price: 250 }));
		expect(screen.getByText("250")).toBeInTheDocument();
		expect(screen.queryByText("OWNED")).toBeNull();
	});

	it("shows an OWNED ribbon instead of the price for an owned item", () => {
		renderCard(makeItem(), { owned: true });
		expect(screen.getByText("OWNED")).toBeInTheDocument();
		expect(screen.queryByText("250")).toBeNull();
	});

	it("wraps the art in a rarity frame that carries data-rarity", () => {
		const { container } = renderCard(makeItem({ rarity: "epic" }));
		expect(container.querySelector('[data-rarity="epic"]')).toBeTruthy();
	});

	it("enables the edge shimmer only for rare and above", () => {
		const common = renderCard(makeItem({ rarity: "common" }));
		expect(common.container.querySelector('[data-shimmer="true"]')).toBeNull();
		cleanup();

		const rare = renderCard(makeItem({ rarity: "rare" }));
		expect(rare.container.querySelector('[data-shimmer="true"]')).toBeTruthy();
	});

	it("crops frame 0 of a strip asset onto the art tile", () => {
		const { container } = renderCard(makeItem());
		const art = container.querySelector(".item-art") as HTMLElement;
		expect(art.style.getPropertyValue("background-image")).toContain("eyes/uni_eyes_dot.png");
		expect(art.style.getPropertyValue("background-size")).toBe("480px 96px");
	});

	it("shows a still asset at its own size", () => {
		const item = makeItem({
			asset: {
				kind: "still",
				path: "head/uni_hat.png",
				bob: [
					[0, 0],
					[0, 0],
					[0, 0],
					[0, 0],
					[0, 0]
				]
			}
		});
		const { container } = renderCard(item);
		const art = container.querySelector(".item-art") as HTMLElement;
		expect(art.style.getPropertyValue("background-image")).toContain("head/uni_hat.png");
		expect(art.style.getPropertyValue("background-size")).toBe("96px 96px");
	});

	it("calls onselect on click", async () => {
		const { onselect } = renderCard(makeItem());
		await fireEvent.click(screen.getByRole("button"));
		expect(onselect).toHaveBeenCalledTimes(1);
	});

	it("calls onselect on Enter", async () => {
		const { onselect } = renderCard(makeItem());
		await fireEvent.keyDown(screen.getByRole("button"), { key: "Enter" });
		expect(onselect).toHaveBeenCalledTimes(1);
	});

	it("is the grid's single tab stop only when selected", () => {
		const selected = renderCard(makeItem(), { selected: true });
		expect(selected.container.querySelector(".item-card")?.getAttribute("tabindex")).toBe("0");
		cleanup();

		const idle = renderCard(makeItem());
		expect(idle.container.querySelector(".item-card")?.getAttribute("tabindex")).toBe("-1");
	});
});
