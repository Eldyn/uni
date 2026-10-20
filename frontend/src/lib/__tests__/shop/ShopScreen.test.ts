// The Shop screen: the marketplace shell (sidebar, hero row, category tiles,
// section tabs and the shelf), the detail panel, the purchase flow, the
// keyboard grid and the backdrop. The economy and avatar stores are mocked so
// the screen runs without a server or a real bake.

import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { cleanup, fireEvent, render, screen, within } from "@testing-library/svelte";
import { flushSync } from "svelte";
import ShopScreen from "$components/shop/ShopScreen.svelte";
import { setCatalogueForTest } from "$lib/avatar/catalogue";
import { setShopBackend, storeShop } from "$lib/stores/shop.svelte";
import { storeAuth } from "$stores/auth.svelte";
import { storeNavigation } from "$stores/navigation.svelte";
import type { BuyResult, PopularItem } from "$lib/shop/shopBackend";
import type { Catalogue } from "$lib/avatar/types";
import { avatarFixture } from "$lib/__tests__/avatar/avatarFixture";
import shopSource from "../../components/shop/ShopScreen.svelte?raw";

const STARTERS = ["uni:round", "uni:eyes_dot", "uni:mouth_smile"];

const ALL_IDS = [
	"uni:round",
	"uni:eyes_dot",
	"uni:eyes_wide",
	"uni:mouth_smile",
	"uni:hat_cap",
	"uni:hat_float",
	"uni:mask_bandit",
	"uni:body_scarf",
	"uni:set_suit",
	"uni:motif_patches"
];

const { shopMock } = vi.hoisted(() => ({
	shopMock: {
		balance: 1500,
		owned: new Set(["uni:round", "uni:eyes_dot", "uni:mouth_smile"]),
		buy: vi.fn(),
		fetchMine: vi.fn(),
		wishlist: vi.fn(),
		toggleWishlist: vi.fn(),
		popular: vi.fn(),
		needsLogin: false
	}
}));

const { bakedSets } = vi.hoisted(() => ({ bakedSets: [] as unknown[] }));

vi.mock("$stores/navigation.svelte", () => ({ storeNavigation: { gotoAuth: vi.fn() } }));

vi.mock("$stores/auth.svelte", () => ({
	storeAuth: { isGuest: false, isLoggedIn: true }
}));

vi.mock("$stores/avatar.svelte", () => ({
	storeAvatar: { loadout: null, fetchMine: vi.fn() }
}));

vi.mock("$lib/avatar/useBakedAvatar.svelte", () => ({
	createBakedAvatar: () => ({
		url: null,
		set: (loadout: unknown) => bakedSets.push(loadout),
		destroy: vi.fn()
	})
}));

function fixture(overrides: Partial<Catalogue> = {}): Catalogue {
	return { ...avatarFixture(), featured: ALL_IDS, ...overrides };
}

function cardIds(container: HTMLElement): string[] {
	return [...container.querySelectorAll<HTMLElement>("[data-item-id]")].map(
		(card) => card.dataset.itemId ?? ""
	);
}

function cardFor(container: HTMLElement, id: string): HTMLElement {
	const card = container.querySelector<HTMLElement>(`[data-item-id="${id}"]`);
	if (!card) throw new Error(`no card for ${id}`);
	return card;
}

function sidebar(container: HTMLElement): HTMLElement {
	const nav = container.querySelector<HTMLElement>(".shop-sidebar");
	if (!nav) throw new Error("no sidebar");
	return nav;
}

beforeEach(async () => {
	vi.spyOn(HTMLCanvasElement.prototype, "getContext").mockReturnValue(null);
	setCatalogueForTest(fixture());
	setShopBackend(shopMock);
	shopMock.balance = 1500;
	shopMock.owned = new Set(STARTERS);
	shopMock.buy.mockReset();
	shopMock.fetchMine.mockReset();
	shopMock.wishlist.mockReset();
	shopMock.wishlist.mockResolvedValue({ ok: true, items: [] });
	shopMock.toggleWishlist.mockReset();
	shopMock.toggleWishlist.mockImplementation(async (itemId: string) => ({
		ok: true,
		items: [itemId]
	}));
	shopMock.popular.mockReset();
	shopMock.popular.mockResolvedValue({ ok: true, items: [] });
	shopMock.needsLogin = false;
	storeAuth.isGuest = false;
	storeAuth.isLoggedIn = true;
	storeNavigation.gotoAuth.mockReset();
	bakedSets.length = 0;
	// INFO: the wishlist and popular shelves live in the store module, so clear
	// the previous test's state before the screen mounts.
	await storeShop.fetchWishlist();
	await storeShop.fetchPopular(7);
});

afterEach(() => {
	cleanup();
	vi.restoreAllMocks();
});

describe("ShopScreen", () => {
	it("has no screen title; the top bar shows it", () => {
		render(ShopScreen);
		flushSync();

		expect(screen.queryByRole("heading", { level: 1 })).not.toBeInTheDocument();
	});

	it("has no balance chip; the top bar carries it", () => {
		const { container } = render(ShopScreen);
		flushSync();

		expect(container.querySelector("[data-balance]")).toBeNull();
	});

	it("keeps the exact pinned root and a single backdrop", () => {
		const { container } = render(ShopScreen);
		flushSync();

		const root = container.querySelector(".shop-screen");
		expect(root).toBeTruthy();
		expect(root?.getAttribute("style") ?? "").toContain("background-color");
		expect(container.querySelectorAll(".shop-backdrop")).toHaveLength(1);
	});

	it("shows the two sidebar groups and every section link", () => {
		const { container } = render(ShopScreen);
		flushSync();

		const nav = sidebar(container);
		const groups = nav.querySelectorAll('[role="group"]');
		expect(groups).toHaveLength(2);
		expect(groups[0]).toHaveTextContent("Discover");

		const links = [...nav.querySelectorAll<HTMLElement>("[data-section]")].map(
			(link) => link.textContent?.trim() ?? ""
		);
		expect(links).toEqual([
			"Featured",
			"Popular",
			"New",
			"Wishlist",
			"Character",
			"Table",
			"Collections"
		]);
		expect(within(nav).getByRole("button", { name: "Featured" }).getAttribute("aria-current")).toBe(
			"page"
		);
	});

	it("renders one large hero and a 2x2 grid of small heroes", () => {
		const { container } = render(ShopScreen);
		flushSync();

		const row = container.querySelector(".shop-hero-row");
		expect(row).toBeTruthy();
		expect(container.querySelectorAll('.shop-hero[data-size="large"]')).toHaveLength(1);
		expect(
			container.querySelectorAll('.shop-hero-grid .shop-hero[data-size="small"]')
		).toHaveLength(4);
	});

	it("renders the three category tiles and selects their section", async () => {
		const { container } = render(ShopScreen);
		flushSync();

		const tiles = [...container.querySelectorAll<HTMLElement>(".category-tile")];
		expect(tiles.map((tile) => tile.dataset.category)).toEqual([
			"character",
			"table",
			"collections"
		]);

		await fireEvent.click(container.querySelector('[data-category="collections"]')!);

		expect(cardIds(container)).toEqual(["uni:set_suit"]);
		expect(
			within(sidebar(container))
				.getByRole("button", { name: "Collections" })
				.getAttribute("aria-current")
		).toBe("page");
		expect(screen.getByRole("tab", { name: "Collections" })).toHaveAttribute(
			"aria-selected",
			"true"
		);
	});

	it("lists the buyable and owned items of the chosen section", async () => {
		const { container } = render(ShopScreen);
		flushSync();

		expect(cardIds(container)).toEqual([
			"uni:round",
			"uni:eyes_dot",
			"uni:mouth_smile",
			"uni:eyes_wide",
			"uni:hat_cap",
			"uni:hat_float",
			"uni:mask_bandit",
			"uni:body_scarf",
			"uni:set_suit",
			"uni:motif_patches"
		]);

		await fireEvent.click(screen.getByRole("tab", { name: "Character" }));
		expect(cardIds(container)).toEqual([
			"uni:round",
			"uni:eyes_dot",
			"uni:mouth_smile",
			"uni:eyes_wide",
			"uni:hat_cap",
			"uni:hat_float",
			"uni:mask_bandit",
			"uni:body_scarf",
			"uni:motif_patches",
			"uni:neon"
		]);

		await fireEvent.click(within(sidebar(container)).getByRole("button", { name: "Collections" }));
		expect(cardIds(container)).toEqual(["uni:set_suit"]);
	});

	it("marks owned items with an OWNED ribbon", () => {
		const { container } = render(ShopScreen);
		flushSync();
		const grid = container.querySelector<HTMLElement>(".shop-grid")!;
		expect(within(grid).getAllByText("OWNED")).toHaveLength(STARTERS.length);
	});

	it("shows the empty-state line for a section with nothing to list", async () => {
		const { container } = render(ShopScreen);
		flushSync();

		await fireEvent.click(screen.getByRole("tab", { name: "Table" }));

		expect(cardIds(container)).toEqual([]);
		expect(screen.getByText(/nothing here/i)).toBeInTheDocument();
	});

	it("shows the empty wishlist shelf when nothing is wanted", async () => {
		const { container } = render(ShopScreen);
		flushSync();

		await fireEvent.click(screen.getByRole("tab", { name: "Wishlist" }));

		expect(cardIds(container)).toEqual([]);
		expect(screen.getByText(/nothing here/i)).toBeInTheDocument();
	});

	it("lists the wanted items on the Wishlist shelf", async () => {
		shopMock.wishlist.mockResolvedValue({ ok: true, items: ["uni:hat_cap"] });
		const { container } = render(ShopScreen);
		flushSync();

		await vi.waitFor(() => expect(storeShop.wishlist.has("uni:hat_cap")).toBe(true));
		await fireEvent.click(screen.getByRole("tab", { name: "Wishlist" }));

		expect(cardIds(container)).toEqual(["uni:hat_cap"]);
	});

	it("renders the marketplace card name, price and wishlist count", () => {
		const { container } = render(ShopScreen);
		flushSync();

		const card = cardFor(container, "uni:hat_cap");
		expect(card.querySelector(".marketplace-name")).toHaveTextContent("Cap");
		expect(card.querySelector(".marketplace-price")).toHaveTextContent("80");
		expect(
			card.querySelector('.marketplace-price [role="img"][aria-label="Blank"]')
		).toBeInTheDocument();
		expect(card.querySelector("[data-wishlist-count]")).toHaveAttribute("data-wishlist-count", "0");
	});

	it("toggles the wishlist without opening the detail drawer", async () => {
		const { container } = render(ShopScreen);
		flushSync();

		const card = cardFor(container, "uni:hat_cap");
		const toggle = card.parentElement?.querySelector<HTMLElement>("[data-wishlist-toggle]");
		expect(toggle).toBeTruthy();
		expect(toggle?.getAttribute("aria-pressed")).toBe("false");
		// INFO: let the mount's wishlist fetch settle before the local toggle.
		await vi.waitFor(() => expect(shopMock.wishlist).toHaveBeenCalledTimes(2));

		await fireEvent.click(toggle!);

		expect(screen.queryByRole("dialog")).toBeNull();
		await vi.waitFor(() => expect(shopMock.toggleWishlist).toHaveBeenCalledWith("uni:hat_cap"));
		await vi.waitFor(() => expect(toggle?.getAttribute("aria-pressed")).toBe("true"));
	});

	it("hides the wishlist controls from guests", () => {
		storeAuth.isGuest = true;
		const { container } = render(ShopScreen);
		flushSync();

		expect(container.querySelector("[data-wishlist-toggle]")).toBeNull();
		expect(container.querySelector("[data-wishlist-count]")).toBeNull();
	});

	it("filters the shelf by rarity and name", async () => {
		const { container } = render(ShopScreen);
		flushSync();

		const rarity = container.querySelector<HTMLSelectElement>('[data-filter="rarity"]')!;
		await fireEvent.change(rarity, { target: { value: "common" } });

		expect(cardIds(container)).toEqual(["uni:eyes_wide", "uni:hat_cap", "uni:hat_float"]);

		const name = container.querySelector<HTMLInputElement>('[data-filter="name"]')!;
		await fireEvent.input(name, { target: { value: "cap" } });

		expect(cardIds(container)).toEqual(["uni:hat_cap"]);
	});

	it("sorts the shelf by popular, price, name and wishlist count", async () => {
		const popular: PopularItem[] = [
			{ item: "uni:motif_patches", purchases: 4, wishlists: 2 },
			{ item: "uni:eyes_dot", purchases: 2, wishlists: 12 }
		];
		shopMock.popular.mockResolvedValue({ ok: true, items: popular });
		setCatalogueForTest(
			fixture({ featured: ["uni:motif_patches", "uni:hat_cap", "uni:eyes_dot"] })
		);
		const { container } = render(ShopScreen);
		flushSync();

		await vi.waitFor(() => expect(storeShop.popular).toHaveLength(2));
		expect(cardIds(container)).toEqual(["uni:motif_patches", "uni:eyes_dot", "uni:hat_cap"]);

		const sort = container.querySelector<HTMLSelectElement>('[data-filter="sort"]')!;
		await fireEvent.change(sort, { target: { value: "price" } });
		expect(cardIds(container)).toEqual(["uni:eyes_dot", "uni:hat_cap", "uni:motif_patches"]);

		await fireEvent.change(sort, { target: { value: "name" } });
		expect(cardIds(container)).toEqual(["uni:hat_cap", "uni:eyes_dot", "uni:motif_patches"]);

		await fireEvent.change(sort, { target: { value: "wishlist" } });
		expect(cardIds(container)).toEqual(["uni:eyes_dot", "uni:motif_patches", "uni:hat_cap"]);
	});

	it("uses the catalogue order for Popular when the shelf is empty", async () => {
		const { container } = render(ShopScreen);
		flushSync();

		await fireEvent.click(screen.getByRole("tab", { name: "Popular" }));

		expect(cardIds(container)).toEqual([
			"uni:round",
			"uni:eyes_dot",
			"uni:eyes_wide",
			"uni:mouth_smile",
			"uni:hat_cap",
			"uni:hat_float",
			"uni:mask_bandit",
			"uni:body_scarf",
			"uni:set_suit",
			"uni:motif_patches"
		]);
	});

	it("lists non-starter items on the New shelf", async () => {
		const { container } = render(ShopScreen);
		flushSync();

		await fireEvent.click(screen.getByRole("tab", { name: "New" }));

		expect(cardIds(container)).toEqual([
			"uni:eyes_wide",
			"uni:hat_cap",
			"uni:hat_float",
			"uni:mask_bandit",
			"uni:body_scarf",
			"uni:set_suit",
			"uni:motif_patches"
		]);
	});

	it("opens the detail panel when a card is selected", async () => {
		const { container } = render(ShopScreen);
		flushSync();

		await fireEvent.click(cardFor(container, "uni:hat_cap"));

		expect(screen.getByRole("dialog")).toBeInTheDocument();
	});

	it("returns focus to the card when the detail drawer closes", async () => {
		const { container } = render(ShopScreen);
		flushSync();

		const card = cardFor(container, "uni:hat_cap");
		await fireEvent.click(card);
		expect(screen.getByRole("dialog")).toBeInTheDocument();

		await fireEvent.keyDown(window, { key: "Escape" });

		await vi.waitFor(() => expect(document.activeElement).toBe(card));
		expect(screen.queryByRole("dialog")).toBeNull();
	});

	it("buys an item and marks the card owned", async () => {
		shopMock.buy.mockImplementation(async (id: string): Promise<BuyResult> => {
			shopMock.owned.add(id);
			shopMock.balance = 1420;
			return { ok: true, balance: 1420 };
		});
		const { container } = render(ShopScreen);
		flushSync();

		await fireEvent.click(cardFor(container, "uni:hat_cap"));
		await fireEvent.click(within(screen.getByRole("dialog")).getByRole("button", { name: "BUY" }));

		await vi.waitFor(() => expect(shopMock.buy).toHaveBeenCalledWith("uni:hat_cap"));
		const grid = container.querySelector<HTMLElement>(".shop-grid")!;
		expect(within(grid).getAllByText("OWNED")).toHaveLength(STARTERS.length + 1);
	});

	it("shows the failure reason and changes nothing when a buy fails", async () => {
		shopMock.buy.mockResolvedValue({ ok: false, reason: "insufficient_cash" });
		const { container } = render(ShopScreen);
		flushSync();

		await fireEvent.click(cardFor(container, "uni:set_suit"));
		const dialog = screen.getByRole("dialog");
		await fireEvent.click(within(dialog).getByRole("button", { name: "BUY" }));

		await vi.waitFor(() =>
			expect(within(dialog).getByRole("alert")).toHaveTextContent("Not enough Blank.")
		);
		expect(container.querySelector("[data-balance]")).toBeNull();
	});

	it("shows the network reason and leaves the card unowned", async () => {
		shopMock.buy.mockResolvedValue({ ok: false, reason: "network" });
		const { container } = render(ShopScreen);
		flushSync();

		await fireEvent.click(cardFor(container, "uni:hat_cap"));
		const dialog = screen.getByRole("dialog");
		await fireEvent.click(within(dialog).getByRole("button", { name: "BUY" }));

		await vi.waitFor(() =>
			expect(within(dialog).getByRole("alert")).toHaveTextContent(/could not reach/i)
		);
		expect(container.querySelector("[data-balance]")).toBeNull();
		const grid = container.querySelector<HTMLElement>(".shop-grid")!;
		expect(within(grid).getAllByText("OWNED")).toHaveLength(STARTERS.length);
	});

	it("fetches the account economy once on mount", async () => {
		render(ShopScreen);
		flushSync();

		await vi.waitFor(() => expect(shopMock.fetchMine).toHaveBeenCalledTimes(1));
	});

	it("does not fetch the economy for a guest", () => {
		storeAuth.isGuest = true;
		render(ShopScreen);
		flushSync();

		expect(shopMock.fetchMine).not.toHaveBeenCalled();
	});

	it("sends an expired session to login instead of a blank shop", async () => {
		shopMock.fetchMine.mockImplementation(async () => {
			shopMock.needsLogin = true;
		});
		render(ShopScreen);
		flushSync();

		await vi.waitFor(() => expect(storeNavigation.gotoAuth).toHaveBeenCalledWith("login"));
	});

	it("leads the hero row with the first featured item", () => {
		setCatalogueForTest(fixture({ featured: ["uni:hat_cap", "uni:mask_bandit"] }));
		const { container } = render(ShopScreen);
		flushSync();
		expect(container.querySelector("[data-featured-id]")?.getAttribute("data-featured-id")).toBe(
			"uni:hat_cap"
		);
	});

	it("renders no heroes when the catalogue has no items", () => {
		setCatalogueForTest(fixture({ featured: [], items: [] }));
		const { container } = render(ShopScreen);
		flushSync();
		expect(container.querySelector("[data-featured-id]")).toBeNull();
	});

	it("shows the featured item worn on the hero preview", () => {
		setCatalogueForTest(fixture({ featured: ["uni:hat_cap"] }));
		render(ShopScreen);
		flushSync();

		expect(bakedSets).toContainEqual(
			expect.objectContaining({
				slots: expect.objectContaining({
					eyes: expect.anything(),
					head_top: expect.objectContaining({ item: "uni:hat_cap" })
				})
			})
		);
	});

	it("moves between section tabs and cards with the arrow keys", async () => {
		const { container } = render(ShopScreen);
		flushSync();

		const tabs = screen.getAllByRole("tab");
		tabs[0].focus();
		await fireEvent.keyDown(tabs[0], { key: "ArrowRight" });
		expect(screen.getByRole("tab", { name: "Popular" })).toHaveAttribute("aria-selected", "true");

		const cards = container.querySelectorAll<HTMLElement>("[data-item-id]");
		cards[0].focus();
		await fireEvent.keyDown(cards[0], { key: "ArrowRight" });
		expect(document.activeElement).toBe(cards[1]);
	});

	it("lays the shell out with a 14rem sidebar and a 2fr hero", () => {
		// jsdom has no layout, so the rules are pinned in the component source.
		expect(shopSource).toMatch(/grid-template-columns:\s*14rem\s+minmax\(0,\s*1fr\)/);
		expect(shopSource).toMatch(/grid-template-columns:\s*minmax\(0,\s*2fr\)\s+minmax\(0,\s*1fr\)/);
	});

	it("stacks the shell on a narrow screen", () => {
		expect(shopSource).toMatch(/@media \(max-width: 900px\) \{/);
		expect(shopSource).toMatch(/\.shop-sidebar-slot \{\s*display: none;\s*\}/);
	});

	it("steps a whole shelf row down with the three-column stride", async () => {
		const { container } = render(ShopScreen);
		flushSync();

		const cards = container.querySelectorAll<HTMLElement>("[data-item-id]");
		expect(cards).toHaveLength(10);
		cards[0].focus();
		await fireEvent.keyDown(cards[0], { key: "ArrowDown" });

		expect(document.activeElement).toBe(cards[3]);
	});

	it("shows each paid palette as one card with its colors", async () => {
		const { container } = render(ShopScreen);
		flushSync();

		await fireEvent.click(screen.getByRole("tab", { name: "Character" }));

		const palettes = [...container.querySelectorAll<HTMLElement>(".palette-card")];
		expect(palettes.map((card) => card.dataset.itemId)).toEqual(["uni:neon"]);
		const cells = [
			...cardFor(container, "uni:neon").querySelectorAll<HTMLElement>(".palette-cell")
		];
		expect(cells.map((cell) => cell.dataset.hex)).toEqual(["#ff2d95", "#00e5ff"]);
		expect(screen.getByText("250")).toBeInTheDocument();
	});

	it("buys a swatch from the palette detail", async () => {
		shopMock.buy.mockImplementation(async (id: string): Promise<BuyResult> => {
			shopMock.owned.add(id);
			shopMock.balance = 1250;
			return { ok: true, balance: 1250 };
		});
		const { container } = render(ShopScreen);
		flushSync();

		await fireEvent.click(screen.getByRole("tab", { name: "Character" }));
		await fireEvent.click(cardFor(container, "uni:neon"));
		const dialog = screen.getByRole("dialog");
		await fireEvent.click(dialog.querySelector('[data-swatch-id="uni:neon:hot"]')!);
		await fireEvent.click(within(dialog).getByRole("button", { name: "BUY" }));

		await vi.waitFor(() => expect(shopMock.buy).toHaveBeenCalledWith("uni:neon:hot"));
		expect(within(dialog).getByText("You already own this.")).toBeInTheDocument();
	});

	it("tries a palette color on the hero preview", async () => {
		setCatalogueForTest(fixture({ featured: ["uni:hat_cap"] }));
		const { container } = render(ShopScreen);
		flushSync();

		await fireEvent.click(screen.getByRole("tab", { name: "Character" }));
		await fireEvent.click(cardFor(container, "uni:neon"));
		await fireEvent.click(
			screen.getByRole("dialog").querySelector('[data-swatch-id="uni:neon:volt"]')!
		);

		expect(bakedSets).toContainEqual(
			expect.objectContaining({
				skin: "uni:neon:volt",
				slots: expect.objectContaining({
					head_top: expect.objectContaining({ item: "uni:hat_cap" })
				})
			})
		);
	});
});
