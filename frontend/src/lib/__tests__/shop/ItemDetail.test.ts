import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { cleanup, fireEvent, render, screen } from "@testing-library/svelte";
import ItemDetail from "$components/shop/ItemDetail.svelte";
import { storeCosmetics } from "$stores/cosmetics.svelte";
import type { BuyFailure } from "$lib/shop/shopBackend";
import { itemPreviewLoadout } from "$lib/avatar/previewLoadout";
import type { CatalogueItem, Loadout } from "$lib/avatar/types";

const baked = vi.hoisted(() => ({ set: vi.fn(), destroy: vi.fn() }));

vi.mock("$lib/avatar/useBakedAvatar.svelte", () => ({
	createBakedAvatar: () => ({
		get url() {
			return "blob:item";
		},
		set: baked.set,
		destroy: baked.destroy
	})
}));

const viewer: Loadout = { shape: "uni:round", skin: "basic:green", slots: {} };

function makeItem(overrides: Partial<CatalogueItem> = {}): CatalogueItem {
	return {
		id: "uni:test",
		slot: "eyes",
		name: "Test Eyes",
		asset: { kind: "strip", path: "eyes/uni_eyes_dot.png" },
		regions: 1,
		rarity: "rare",
		price: 600,
		...overrides
	};
}

function renderDetail(item: CatalogueItem, props: Record<string, unknown> = {}) {
	const handlers = { onbuy: vi.fn(), ontry: vi.fn(), onclose: vi.fn() };
	const utils = render(ItemDetail, {
		props: {
			item,
			owned: false,
			balance: 1500,
			guest: false,
			error: null,
			loadout: viewer,
			...handlers,
			...props
		}
	});
	return { ...handlers, ...utils };
}

beforeEach(() => {
	storeCosmetics.reset();
});

afterEach(cleanup);

describe("ItemDetail", () => {
	it("shows the name, the price and the rarity frame", () => {
		const { container } = renderDetail(makeItem());
		expect(screen.getByText("uni:test")).toBeInTheDocument();
		expect(screen.getByText("600")).toBeInTheDocument();
		expect(container.querySelector('[data-rarity="rare"]')).toBeTruthy();
	});

	it("bakes the item with default colors on the detail art", () => {
		baked.set.mockClear();
		const item = makeItem();
		const { container } = renderDetail(item, { owned: false });

		expect(container.querySelector(".item-art")?.getAttribute("style") ?? "").not.toContain(
			"background-image"
		);
		expect(baked.set).toHaveBeenCalledWith(itemPreviewLoadout(item, viewer, false));
	});

	it("disables BUY and shows the reason when the balance is below the price", () => {
		renderDetail(makeItem(), { balance: 100 });
		expect(screen.getByRole("button", { name: /buy/i })).toBeDisabled();
		expect(screen.getByText(/not enough blank/i)).toBeInTheDocument();
	});

	it("styles BUY as the primary button and TRY ON as the secondary button", () => {
		renderDetail(makeItem());
		expect(screen.getByRole("button", { name: /buy/i })).toHaveClass("btn");
		expect(screen.getByRole("button", { name: /try on/i })).toHaveClass("btn-secondary");
	});

	it("enables BUY when the balance covers the price and calls onbuy", async () => {
		const { onbuy } = renderDetail(makeItem());
		const buy = screen.getByRole("button", { name: /buy/i });
		expect(buy).toBeEnabled();

		await fireEvent.click(buy);
		expect(onbuy).toHaveBeenCalledTimes(1);
	});

	it("shows Register to buy for a guest and never calls onbuy", async () => {
		const { onbuy } = renderDetail(makeItem(), { guest: true });
		const buy = screen.getByRole("button", { name: /register to buy/i });
		expect(buy).toBeDisabled();

		await fireEvent.click(buy);
		expect(onbuy).not.toHaveBeenCalled();
	});

	it("keeps TRY ON enabled and calls ontry for a guest", async () => {
		const { ontry } = renderDetail(makeItem(), { guest: true });
		const tryOn = screen.getByRole("button", { name: /try on/i });
		expect(tryOn).toBeEnabled();

		await fireEvent.click(tryOn);
		expect(ontry).toHaveBeenCalledTimes(1);
	});

	it("keeps TRY ON enabled when the balance is too low", async () => {
		const { ontry } = renderDetail(makeItem(), { balance: 0 });
		const tryOn = screen.getByRole("button", { name: /try on/i });
		expect(tryOn).toBeEnabled();

		await fireEvent.click(tryOn);
		expect(ontry).toHaveBeenCalledTimes(1);
	});

	it("disables BUY for an already owned item", () => {
		renderDetail(makeItem(), { owned: true });
		expect(screen.getByRole("button", { name: /buy/i })).toBeDisabled();
	});

	it("calls onclose on Escape", async () => {
		const { onclose } = renderDetail(makeItem());
		await fireEvent.keyDown(window, { key: "Escape" });
		expect(onclose).toHaveBeenCalledTimes(1);
	});

	const cosmetic = makeItem({ id: "uni:face_neon", slot: "card_face" });

	it("shows Use instead of BUY and TRY ON for an owned cosmetic", () => {
		renderDetail(cosmetic, { owned: true });

		const use = screen.getByRole("button", { name: /^use$/i });
		expect(use).toBeEnabled();
		expect(use).toHaveAttribute("data-cosmetic-use");
		expect(screen.queryByRole("button", { name: /buy/i })).toBeNull();
		expect(screen.queryByRole("button", { name: /try on/i })).toBeNull();
	});

	it("shows a disabled Equipped state for the equipped cosmetic", () => {
		storeCosmetics.slots = { card_face: cosmetic.id, card_back: null, playmat: null };

		renderDetail(cosmetic, { owned: true });

		const equipped = screen.getByRole("button", { name: /^equipped$/i });
		expect(equipped).toBeDisabled();
		expect(equipped).toHaveAttribute("data-cosmetic-equipped");
		expect(screen.queryByRole("button", { name: /^use$/i })).toBeNull();
	});

	it("equips the owned cosmetic when Use is clicked", async () => {
		const equip = vi.spyOn(storeCosmetics, "equip").mockResolvedValue({ ok: true });

		renderDetail(cosmetic, { owned: true });
		await fireEvent.click(screen.getByRole("button", { name: /^use$/i }));

		expect(equip).toHaveBeenCalledWith("card_face", cosmetic.id);
		equip.mockRestore();
	});

	it("keeps BUY and TRY ON for a non-cosmetic item", () => {
		renderDetail(makeItem(), { owned: true });

		expect(screen.queryByRole("button", { name: /^use$/i })).toBeNull();
		expect(screen.getByRole("button", { name: /buy/i })).toBeInTheDocument();
		expect(screen.getByRole("button", { name: /try on/i })).toBeInTheDocument();
	});

	const FAILURES: [BuyFailure, RegExp][] = [
		["insufficient_cash", /not enough blank/i],
		["already_owned", /already own/i],
		["not_available", /no longer available/i],
		["unknown_item", /does not exist/i],
		["guest", /register to buy/i]
	];

	for (const [reason, expected] of FAILURES) {
		it(`shows the ${reason} reason line for a failed buy`, () => {
			renderDetail(makeItem(), { error: reason });
			expect(screen.getByRole("alert")).toHaveTextContent(expected);
			cleanup();
		});
	}
});
