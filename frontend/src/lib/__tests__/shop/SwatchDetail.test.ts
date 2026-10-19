import { afterEach, describe, expect, it, vi } from "vitest";
import { cleanup, fireEvent, render, screen } from "@testing-library/svelte";
import SwatchDetail from "$components/shop/SwatchDetail.svelte";
import type { Palette, Swatch } from "$lib/avatar/types";

const PALETTE: Palette = { id: "uni:neon", name: "Neon", price: 250, swatches: [] };
const SWATCH: Swatch = {
	id: "uni:neon:hot",
	hex: "#ff2d95",
	price: 250,
	rarity: "rare"
};

function renderDetail(props: Record<string, unknown> = {}) {
	const onbuy = vi.fn();
	const onclose = vi.fn();
	const utils = render(SwatchDetail, {
		props: {
			swatch: SWATCH,
			palette: PALETTE,
			owned: false,
			balance: 500,
			guest: false,
			onbuy,
			onclose,
			...props
		}
	});
	return { onbuy, onclose, ...utils };
}

afterEach(cleanup);

describe("SwatchDetail", () => {
	it("shows the name, palette, rarity and price", () => {
		renderDetail();

		expect(screen.getByText("Hot")).toBeInTheDocument();
		expect(screen.getByText("Neon")).toBeInTheDocument();
		expect(screen.getByText("250")).toBeInTheDocument();
	});

	it("disables BUY when the balance is too low", () => {
		renderDetail({ balance: 100 });

		expect(screen.getByRole("button", { name: "BUY" })).toBeDisabled();
		expect(screen.getByText(/not enough cash/i)).toBeInTheDocument();
	});

	it("shows Register to buy for a guest", () => {
		renderDetail({ guest: true });

		expect(screen.getByRole("button", { name: "Register to buy" })).toBeDisabled();
	});

	it("calls onbuy and onclose", async () => {
		const { onbuy, onclose } = renderDetail();

		await fireEvent.click(screen.getByRole("button", { name: "BUY" }));
		expect(onbuy).toHaveBeenCalledTimes(1);

		await fireEvent.keyDown(window, { key: "Escape" });
		expect(onclose).toHaveBeenCalledTimes(1);
	});

	it("shows a failure reason", () => {
		renderDetail({ error: "insufficient_cash" });

		expect(screen.getByRole("alert")).toHaveTextContent("Not enough cash.");
	});
});
