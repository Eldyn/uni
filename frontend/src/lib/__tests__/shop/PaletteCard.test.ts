import { afterEach, describe, expect, it, vi } from "vitest";
import { cleanup, fireEvent, render, screen } from "@testing-library/svelte";
import PaletteCard from "$components/shop/PaletteCard.svelte";
import type { Palette } from "$lib/avatar/types";
import swatchesSource from "../../components/shop/PaletteSwatches.svelte?raw";

const PALETTE: Palette = {
	id: "uni:neon",
	name: "Neon",
	price: 250,
	swatches: [
		{ id: "uni:neon:hot", hex: "#ff2d95", price: 250, rarity: "rare" },
		{ id: "uni:neon:volt", hex: "#00e5ff", price: 600, rarity: "epic" }
	]
};

function renderCard(props: Record<string, unknown> = {}) {
	const onselect = vi.fn();
	const utils = render(PaletteCard, {
		props: { palette: PALETTE, price: 250, selected: true, onselect, ...props }
	});
	return { onselect, ...utils };
}

/** The declarations of one CSS rule, read straight from the component source. */
function cssBlock(source: string, selector: string): string {
	const escaped = selector.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
	const match = source.match(new RegExp(`${escaped}\\s*\\{([^}]*)\\}`));
	if (!match) throw new Error(`rule not found: ${selector}`);
	return match[1];
}

afterEach(cleanup);

describe("PaletteCard", () => {
	it("shows every color, the name, the rarity and the price", () => {
		const { container } = renderCard();

		expect(
			[...container.querySelectorAll<HTMLElement>(".palette-cell")].map((cell) => cell.dataset.hex)
		).toEqual(["#ff2d95", "#00e5ff"]);
		expect(screen.getByText("Neon")).toBeInTheDocument();
		expect(screen.getByText("250")).toBeInTheDocument();
		expect(container.querySelector('[data-rarity="epic"]')).not.toBeNull();
		expect(container.querySelector(".pixel-bordered")).toBeNull();
	});

	it("shows OWNED when no swatch is left to buy", () => {
		renderCard({ price: null });

		expect(screen.getByText("OWNED")).toBeInTheDocument();
	});

	it("calls onselect on click and Enter", async () => {
		const { container, onselect } = renderCard();
		const card = container.querySelector<HTMLElement>(".palette-card");
		if (!card) throw new Error("no palette card");

		await fireEvent.click(card);
		await fireEvent.keyDown(card, { key: "Enter" });
		expect(onselect).toHaveBeenCalledTimes(2);
	});

	it("fills the card with a fluid six-column grid of square cells", () => {
		const grid = cssBlock(swatchesSource, ".palette-swatches");
		expect(grid).toMatch(/width:\s*100%/);
		expect(grid).toMatch(/grid-template-columns:\s*repeat\(6, minmax\(0, 1fr\)\)/);
		expect(grid).toMatch(/grid-auto-rows:\s*1fr/);
		// INFO: the cap keeps a wide card from growing past the requested cell size.
		expect(swatchesSource).toContain("--swatch-cap: {cap}");
		expect(swatchesSource).toContain("const cap = $derived(`${size * 6 + 5}px`);");
		expect(grid).toMatch(/max-width:\s*var\(--swatch-cap\)/);
		expect(cssBlock(swatchesSource, ".palette-cell")).toMatch(/aspect-ratio:\s*1/);
	});
});
