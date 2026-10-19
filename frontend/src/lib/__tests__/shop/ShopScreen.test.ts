// The Shop screen: tab filtering, the detail panel, the purchase flow, the
// featured banner, the keyboard grid and the backdrop. The economy and avatar
// stores are mocked so the screen runs without a server or a real bake.

import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { cleanup, fireEvent, render, screen, within } from "@testing-library/svelte";
import { flushSync } from "svelte";
import ShopScreen from "$components/shop/ShopScreen.svelte";
import { setCatalogueForTest } from "$lib/avatar/catalogue";
import { setShopBackend } from "$lib/stores/shop.svelte";
import { storeAuth } from "$stores/auth.svelte";
import { storeNavigation } from "$stores/navigation.svelte";
import type { BuyResult } from "$lib/shop/shopBackend";
import type { Catalogue } from "$lib/avatar/types";
import { avatarFixture } from "$lib/__tests__/avatar/avatarFixture";

const STARTERS = ["uni:round", "uni:eyes_dot", "uni:mouth_smile"];

const { shopMock } = vi.hoisted(() => ({
	shopMock: {
		balance: 1500,
		owned: new Set(["uni:round", "uni:eyes_dot", "uni:mouth_smile"]),
		buy: vi.fn(),
		fetchMine: vi.fn(),
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
	return { ...avatarFixture(), ...overrides };
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

beforeEach(() => {
	vi.spyOn(HTMLCanvasElement.prototype, "getContext").mockReturnValue(null);
	setCatalogueForTest(fixture());
	setShopBackend(shopMock);
	shopMock.balance = 1500;
	shopMock.owned = new Set(STARTERS);
	shopMock.buy.mockReset();
	shopMock.fetchMine.mockReset();
	shopMock.needsLogin = false;
	storeAuth.isGuest = false;
	storeAuth.isLoggedIn = true;
	storeNavigation.gotoAuth.mockReset();
	bakedSets.length = 0;
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

	it("lists the buyable and owned items of the chosen tab", async () => {
		const { container } = render(ShopScreen);
		flushSync();

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

		await fireEvent.click(screen.getByRole("tab", { name: "Hats" }));
		expect(cardIds(container)).toEqual(["uni:hat_cap", "uni:hat_float"]);
	});

	it("marks owned items with an OWNED ribbon", () => {
		render(ShopScreen);
		flushSync();
		expect(screen.getAllByText("OWNED")).toHaveLength(STARTERS.length);
	});

	it("shows the empty-state line for a tab with nothing to list", async () => {
		setCatalogueForTest(fixture({ palettes: [] }));
		const { container } = render(ShopScreen);
		flushSync();

		await fireEvent.click(screen.getByRole("tab", { name: "Palettes" }));

		expect(cardIds(container)).toEqual([]);
		expect(screen.getByText(/nothing here/i)).toBeInTheDocument();
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

	it("buys an item, updates the balance chip and marks the card owned", async () => {
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
		expect(container.querySelector("[data-balance]")?.getAttribute("data-balance")).toBe("1420");
		expect(screen.getAllByText("OWNED")).toHaveLength(STARTERS.length + 1);
	});

	it("shows the failure reason and changes nothing when a buy fails", async () => {
		shopMock.buy.mockResolvedValue({ ok: false, reason: "insufficient_cash" });
		const { container } = render(ShopScreen);
		flushSync();

		await fireEvent.click(cardFor(container, "uni:set_suit"));
		const dialog = screen.getByRole("dialog");
		await fireEvent.click(within(dialog).getByRole("button", { name: "BUY" }));

		await vi.waitFor(() =>
			expect(within(dialog).getByRole("alert")).toHaveTextContent("Not enough cash.")
		);
		expect(container.querySelector("[data-balance]")?.getAttribute("data-balance")).toBe("1500");
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
		expect(container.querySelector("[data-balance]")?.getAttribute("data-balance")).toBe("1500");
		expect(screen.getAllByText("OWNED")).toHaveLength(STARTERS.length);
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

	it("shows the first featured item and hides when the list is empty", () => {
		setCatalogueForTest(fixture({ featured: ["uni:hat_cap", "uni:mask_bandit"] }));
		const { container } = render(ShopScreen);
		flushSync();
		expect(container.querySelector("[data-featured-id]")?.getAttribute("data-featured-id")).toBe(
			"uni:hat_cap"
		);

		cleanup();
		setCatalogueForTest(fixture({ featured: [] }));
		const empty = render(ShopScreen);
		flushSync();
		expect(empty.container.querySelector("[data-featured-id]")).toBeNull();
	});

	it("shows the featured item worn on the banner preview", () => {
		setCatalogueForTest(fixture({ featured: ["uni:hat_cap"] }));
		render(ShopScreen);
		flushSync();

		// INFO: item tiles bake too, so find the full-body banner look.
		expect(bakedSets).toContainEqual(
			expect.objectContaining({
				slots: expect.objectContaining({
					eyes: expect.anything(),
					head_top: expect.objectContaining({ item: "uni:hat_cap" })
				})
			})
		);
	});

	it("moves between tabs and cards with the arrow keys", async () => {
		const { container } = render(ShopScreen);
		flushSync();

		const tabs = screen.getAllByRole("tab");
		tabs[0].focus();
		await fireEvent.keyDown(tabs[0], { key: "ArrowRight" });
		expect(screen.getByRole("tab", { name: "Eyes" })).toHaveAttribute("aria-selected", "true");

		const cards = container.querySelectorAll<HTMLElement>("[data-item-id]");
		cards[0].focus();
		await fireEvent.keyDown(cards[0], { key: "ArrowRight" });
		expect(document.activeElement).toBe(cards[1]);
	});

	it("mounts a single backdrop", () => {
		const { container } = render(ShopScreen);
		flushSync();
		expect(container.querySelectorAll(".shop-backdrop")).toHaveLength(1);
	});

	it("lists the paid swatches in the Palettes tab", async () => {
		const { container } = render(ShopScreen);
		flushSync();

		await fireEvent.click(screen.getByRole("tab", { name: "Palettes" }));

		expect(cardIds(container)).toEqual(["uni:neon:hot", "uni:neon:volt"]);
		expect(screen.getByText("250")).toBeInTheDocument();
		expect(screen.getByText("600")).toBeInTheDocument();
	});

	it("buys a swatch and marks it owned", async () => {
		shopMock.buy.mockImplementation(async (id: string): Promise<BuyResult> => {
			shopMock.owned.add(id);
			shopMock.balance = 1250;
			return { ok: true, balance: 1250 };
		});
		const { container } = render(ShopScreen);
		flushSync();

		await fireEvent.click(screen.getByRole("tab", { name: "Palettes" }));
		await fireEvent.click(cardFor(container, "uni:neon:hot"));
		await fireEvent.click(within(screen.getByRole("dialog")).getByRole("button", { name: "BUY" }));

		await vi.waitFor(() => expect(shopMock.buy).toHaveBeenCalledWith("uni:neon:hot"));
		expect(container.querySelector("[data-balance]")?.getAttribute("data-balance")).toBe("1250");
		expect(screen.getAllByText("OWNED")).toHaveLength(1);
	});
});
