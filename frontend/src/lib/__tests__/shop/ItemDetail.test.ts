import { afterEach, describe, expect, it, vi } from "vitest";
import { cleanup, fireEvent, render, screen } from "@testing-library/svelte";
import ItemDetail from "$components/shop/ItemDetail.svelte";
import type { BuyFailure } from "$lib/shop/shopBackend";
import type { CatalogueItem } from "$lib/avatar/types";

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
		props: { item, owned: false, balance: 1500, guest: false, error: null, ...handlers, ...props }
	});
	return { ...handlers, ...utils };
}

afterEach(cleanup);

describe("ItemDetail", () => {
	it("shows the name, the price and the rarity frame", () => {
		const { container } = renderDetail(makeItem());
		expect(screen.getByText("uni:test")).toBeInTheDocument();
		expect(screen.getByText("600")).toBeInTheDocument();
		expect(container.querySelector('[data-rarity="rare"]')).toBeTruthy();
	});

	it("disables BUY and shows the reason when the balance is below the price", () => {
		renderDetail(makeItem(), { balance: 100 });
		expect(screen.getByRole("button", { name: /buy/i })).toBeDisabled();
		expect(screen.getByText(/not enough cash/i)).toBeInTheDocument();
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

	const FAILURES: [BuyFailure, RegExp][] = [
		["insufficient_cash", /not enough cash/i],
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
