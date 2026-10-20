// The marketplace card: name top centre in Pypx thick, art in the middle, the
// wishlist count bottom left and the price bottom right. The card opens the
// detail drawer; the wishlist toggle is a separate control that does not.

import { afterEach, describe, expect, it, vi } from "vitest";
import { cleanup, fireEvent, render, screen } from "@testing-library/svelte";
import MarketplaceCard from "$components/shop/MarketplaceCard.svelte";
import cardSource from "../../components/shop/MarketplaceCard.svelte?raw";
import type { CatalogueItem, Loadout } from "$lib/avatar/types";

vi.mock("$lib/avatar/useBakedAvatar.svelte", () => ({
	createBakedAvatar: () => ({
		url: "blob:card",
		set: vi.fn(),
		destroy: vi.fn()
	})
}));

const viewer: Loadout = { shape: "uni:round", skin: "basic:green", slots: {} };

function makeItem(overrides: Partial<CatalogueItem> = {}): CatalogueItem {
	return {
		id: "uni:hat_cap",
		slot: "head_top",
		name: "Cap",
		asset: { kind: "still", path: "head_top/uni_hat_cap.png", bob: [[0, 0]] },
		regions: 1,
		rarity: "common",
		price: 80,
		...overrides
	};
}

function renderCard(item: CatalogueItem = makeItem(), props: Record<string, unknown> = {}) {
	const onselect = vi.fn();
	const ontoggle = vi.fn();
	const utils = render(MarketplaceCard, {
		props: {
			item,
			owned: false,
			selected: true,
			loadout: viewer,
			wishlisted: false,
			wishlistCount: 7,
			showWishlist: true,
			onselect,
			ontoggle,
			...props
		}
	});
	return { onselect, ontoggle, ...utils };
}

/** The declarations of one CSS rule, read straight from the component source. */
function cssBlock(source: string, selector: string): string {
	const escaped = selector.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
	const match = source.match(new RegExp(`${escaped}\\s*\\{([^}]*)\\}`));
	if (!match) throw new Error(`rule not found: ${selector}`);
	return match[1];
}

afterEach(cleanup);

describe("MarketplaceCard", () => {
	it("puts the name top centre in Pypx thick", () => {
		const block = cssBlock(cardSource, ".marketplace-name");
		expect(block).toMatch(/font-family:\s*var\(--pypx\)/);
		expect(block).toMatch(/font-weight:\s*800/);
		expect(block).toMatch(/text-align:\s*center/);
	});

	it("shows the name, the price and the Blank icon", () => {
		renderCard();

		expect(screen.getByText("Cap")).toBeInTheDocument();
		expect(document.querySelector(".marketplace-price")).toHaveTextContent("80");
		expect(screen.getByRole("img", { name: "Blank" })).toBeInTheDocument();
	});

	it("shows the wishlist count with a bookmark icon", () => {
		const { container } = renderCard(makeItem(), { wishlistCount: 12 });

		const count = container.querySelector("[data-wishlist-count]");
		expect(count).toHaveAttribute("data-wishlist-count", "12");
		expect(count).toHaveTextContent("12");
		expect(count?.querySelector(".marketplace-bookmark")).toBeInTheDocument();
	});

	it("marks the wishlist toggle pressed when the item is wanted", () => {
		const { container } = renderCard(makeItem(), { wishlisted: true });

		expect(container.querySelector("[data-wishlist-toggle]")).toHaveAttribute(
			"aria-pressed",
			"true"
		);
	});

	it("toggles the wishlist without opening the detail", async () => {
		const { container, onselect, ontoggle } = renderCard();

		await fireEvent.click(container.querySelector("[data-wishlist-toggle]")!);

		expect(ontoggle).toHaveBeenCalledTimes(1);
		expect(onselect).not.toHaveBeenCalled();
	});

	it("opens the detail when the rest of the card is clicked", async () => {
		const { onselect } = renderCard();

		await fireEvent.click(screen.getByRole("button", { name: /cap/i }));

		expect(onselect).toHaveBeenCalledTimes(1);
	});

	it("hides the count and the toggle for guests", () => {
		const { container } = renderCard(makeItem(), { showWishlist: false });

		expect(container.querySelector("[data-wishlist-count]")).toBeNull();
		expect(container.querySelector("[data-wishlist-toggle]")).toBeNull();
	});

	it("shows an OWNED ribbon instead of the price for an owned item", () => {
		const { container } = renderCard(makeItem(), { owned: true });

		expect(container.querySelector(".marketplace-owned")).toHaveTextContent("OWNED");
		expect(container.querySelector(".marketplace-price")).toBeNull();
	});

	it("is the grid's single tab stop only when selected", () => {
		const selected = renderCard(makeItem(), { selected: true });
		expect(selected.container.querySelector("[data-item-id]")).toHaveAttribute("tabindex", "0");
		cleanup();

		const idle = renderCard(makeItem(), { selected: false });
		expect(idle.container.querySelector("[data-item-id]")).toHaveAttribute("tabindex", "-1");
	});
});
