// The Shop screen: tab filtering, the detail panel, the purchase flow, the
// featured banner, the keyboard grid and the backdrop. The economy and avatar
// stores are mocked so the screen runs without a server or a real bake.

import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { cleanup, fireEvent, render, screen, within } from "@testing-library/svelte";
import { flushSync } from "svelte";
import ShopScreen from "$components/shop/ShopScreen.svelte";
import { setCatalogueForTest } from "$lib/avatar/catalogue";
import type { BuyResult } from "$lib/shop/shopBackend";
import type { Catalogue } from "$lib/avatar/types";
import { avatarFixture } from "$lib/__tests__/avatar/avatarFixture";

const STARTERS = ["uni:round", "uni:eyes_dot", "uni:mouth_smile"];

const { shopMock } = vi.hoisted(() => ({
	shopMock: {
		balance: 1500,
		owned: new Set(["uni:round", "uni:eyes_dot", "uni:mouth_smile"]),
		buy: vi.fn()
	}
}));

vi.mock("$lib/stores/shopMock.svelte", () => ({ shopMock }));

vi.mock("$stores/auth.svelte", () => ({
	storeAuth: { isGuest: false, isLoggedIn: true }
}));

vi.mock("$stores/avatar.svelte", () => ({
	storeAvatar: { loadout: null, fetchMine: vi.fn() }
}));

vi.mock("$lib/avatar/useBakedAvatar.svelte", () => ({
	createBakedAvatar: () => ({ url: null, set: vi.fn(), destroy: vi.fn() })
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
	shopMock.balance = 1500;
	shopMock.owned = new Set(STARTERS);
	shopMock.buy.mockReset();
});

afterEach(() => {
	cleanup();
	vi.restoreAllMocks();
});

describe("ShopScreen", () => {
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
});
