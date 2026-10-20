// The marketplace hero tile: the item name sits top centre in Pypx thick and
// the price bottom right with the Blank icon. A set or collection prefers its
// wide hero image and falls back to the baked avatar preview when the file is
// missing. Only the large hero carries BUY / TRY ON.

import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { cleanup, fireEvent, render, screen } from "@testing-library/svelte";
import type { CatalogueItem } from "$lib/avatar/types";
import { fullLoadout } from "$lib/__tests__/avatar/avatarFixture";

const bakedState = vi.hoisted(() => ({ url: null as string | null }));

vi.mock("$lib/avatar/useBakedAvatar.svelte", () => ({
	createBakedAvatar: () => ({
		get url() {
			return bakedState.url;
		},
		set: vi.fn(),
		destroy: vi.fn()
	})
}));

import ShopHero from "$components/shop/ShopHero.svelte";
import heroSource from "../../components/shop/ShopHero.svelte?raw";

function makeItem(overrides: Partial<CatalogueItem> = {}): CatalogueItem {
	return {
		id: "uni:hat_cap",
		slot: "head_top",
		name: "Cap",
		asset: {
			kind: "still",
			path: "head_top/uni_hat_cap.png",
			bob: [[0, 0]] as [number, number][]
		},
		regions: 1,
		rarity: "rare",
		price: 80,
		...overrides
	};
}

function setItem(overrides: Partial<CatalogueItem> = {}): CatalogueItem {
	return makeItem({
		id: "uni:set_suit",
		slot: "set",
		name: "Suit",
		asset: { kind: "strip", path: "set/uni_set_suit.png" },
		regions: 0,
		rarity: "epic",
		price: 900,
		...overrides
	});
}

function renderHero(
	item: CatalogueItem = makeItem(),
	size: "large" | "small" = "small",
	owned = false
) {
	return render(ShopHero, {
		props: {
			item,
			owned,
			balance: 1500,
			guest: false,
			loadout: fullLoadout(),
			onbuy: vi.fn(),
			ontry: vi.fn(),
			onselect: vi.fn(),
			size
		}
	});
}

function cssBlock(source: string, selector: string): string {
	const escaped = selector.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
	const match = source.match(new RegExp(`${escaped}\\s*\\{([^}]*)\\}`));
	if (!match) throw new Error(`rule not found: ${selector}`);
	return match[1];
}

beforeEach(() => {
	bakedState.url = "blob:avatar";
});

afterEach(cleanup);

describe("ShopHero", () => {
	it("puts the name top centre in Pypx thick and the price bottom right", () => {
		const { container } = renderHero();

		expect(container.querySelector(".hero-name")).toHaveTextContent("Cap");
		const price = container.querySelector(".hero-price");
		expect(price).toHaveTextContent("80");
		expect(price?.querySelector(".blank-icon")).toBeTruthy();

		expect(cssBlock(heroSource, ".hero-name")).toMatch(/font-family:\s*var\(--pypx\)/);
		expect(cssBlock(heroSource, ".hero-name")).toMatch(/font-weight:\s*800/);
		expect(cssBlock(heroSource, ".hero-name")).toMatch(/text-align:\s*center/);
		expect(cssBlock(heroSource, ".hero-price")).toMatch(/right:\s*0/);
		expect(cssBlock(heroSource, ".hero-price")).toMatch(/bottom:\s*0/);
	});

	it("draws a plain item on the baked avatar preview", () => {
		const { container } = renderHero();

		expect(container.querySelector(".hero-image")).toBeNull();
		expect(container.querySelector(".avatar-sprite")).toBeTruthy();
	});

	it("prefers the wide hero image for a set", () => {
		const { container } = renderHero(setItem());

		const image = container.querySelector<HTMLImageElement>(".hero-image");
		expect(image?.getAttribute("src")).toContain("/assets/shop/heroes/uni_set_suit.png");
	});

	it("falls back to the avatar preview when the hero image is missing", async () => {
		const { container } = renderHero(setItem());

		const image = container.querySelector<HTMLImageElement>(".hero-image")!;
		await fireEvent.error(image);

		expect(container.querySelector(".hero-image")).toBeNull();
		expect(container.querySelector(".avatar-sprite")).toBeTruthy();
	});

	it("shows BUY and TRY ON only on the large hero", () => {
		const large = renderHero(makeItem(), "large");
		expect(screen.getByRole("button", { name: "BUY" })).toBeInTheDocument();
		expect(screen.getByRole("button", { name: "TRY ON" })).toBeInTheDocument();
		cleanup();

		renderHero(makeItem(), "small");
		expect(screen.queryByRole("button", { name: "BUY" })).toBeNull();
		expect(screen.queryByRole("button", { name: "TRY ON" })).toBeNull();
		expect(large).toBeTruthy();
	});

	it("shows OWNED instead of a price for an owned item", () => {
		const { container } = renderHero(makeItem(), "small", true);

		expect(container.querySelector(".hero-owned")).toHaveTextContent("OWNED");
		expect(container.querySelector(".hero-price")).toBeNull();
	});

	it("asks the screen to open the item when the tile is pressed", async () => {
		const onselect = vi.fn();
		render(ShopHero, {
			props: {
				item: makeItem(),
				owned: false,
				balance: 1500,
				guest: false,
				loadout: fullLoadout(),
				onbuy: vi.fn(),
				ontry: vi.fn(),
				onselect,
				size: "small"
			}
		});

		await fireEvent.click(screen.getByRole("button", { name: "Cap" }));
		expect(onselect).toHaveBeenCalledTimes(1);
	});
});
