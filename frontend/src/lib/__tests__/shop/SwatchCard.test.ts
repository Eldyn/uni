import { afterEach, describe, expect, it, vi } from "vitest";
import { cleanup, fireEvent, render, screen } from "@testing-library/svelte";
import SwatchCard from "$components/shop/SwatchCard.svelte";
import type { Palette, Swatch } from "$lib/avatar/types";

const PALETTE: Palette = { id: "uni:neon", name: "Neon", price: 250, swatches: [] };
const SWATCH: Swatch = {
	id: "uni:neon:hot",
	hex: "#ff2d95",
	price: 250,
	rarity: "uncommon"
};

function renderCard(props: Record<string, unknown> = {}) {
	const onselect = vi.fn();
	const utils = render(SwatchCard, {
		props: { swatch: SWATCH, palette: PALETTE, owned: false, selected: true, onselect, ...props }
	});
	return { onselect, ...utils };
}

afterEach(cleanup);

describe("SwatchCard", () => {
	it("shows the name, color, palette, rarity and price", () => {
		const { container } = renderCard();

		expect(screen.getByText("Hot")).toBeInTheDocument();
		expect(screen.getByText("Neon")).toBeInTheDocument();
		expect(screen.getByText("250")).toBeInTheDocument();
		expect(container.querySelector('[data-rarity="uncommon"]')).not.toBeNull();
		expect(
			container.querySelector<HTMLElement>(".swatch-tile")?.style.getPropertyValue("--swatch")
		).toBe("#ff2d95");
	});

	it("shows OWNED instead of a price", () => {
		renderCard({ owned: true });

		expect(screen.getByText("OWNED")).toBeInTheDocument();
		expect(screen.queryByText("250")).toBeNull();
	});

	it("calls onselect on click and Enter", async () => {
		const { container, onselect } = renderCard();
		const card = container.querySelector<HTMLElement>(".swatch-card");
		if (!card) throw new Error("no swatch card");

		await fireEvent.click(card);
		await fireEvent.keyDown(card, { key: "Enter" });
		expect(onselect).toHaveBeenCalledTimes(2);
	});
});
