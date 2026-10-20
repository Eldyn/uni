import { afterEach, describe, expect, it, vi } from "vitest";
import { cleanup, fireEvent, render, screen } from "@testing-library/svelte";
import PaletteDetail from "$components/shop/PaletteDetail.svelte";
import type { Loadout, Palette } from "$lib/avatar/types";

const baked = vi.hoisted(() => ({ set: vi.fn(), destroy: vi.fn() }));

vi.mock("$lib/avatar/useBakedAvatar.svelte", () => ({
	createBakedAvatar: () => ({
		get url() {
			return "blob:palette";
		},
		set: baked.set,
		destroy: baked.destroy
	})
}));

const PALETTE: Palette = {
	id: "uni:neon",
	name: "Neon",
	price: 250,
	swatches: [
		{ id: "uni:neon:hot", hex: "#ff2d95", price: 250, rarity: "rare" },
		{ id: "uni:neon:volt", hex: "#00e5ff", price: 600, rarity: "epic" },
		{ id: "uni:neon:dim", hex: "#333333" }
	]
};

const viewer: Loadout = { shape: "uni:round", skin: "basic:red", slots: {} };

function renderDetail(props: Record<string, unknown> = {}) {
	const handlers = { onbuy: vi.fn(), ontry: vi.fn(), onclose: vi.fn() };
	const utils = render(PaletteDetail, {
		props: {
			palette: PALETTE,
			owned: new Set<string>(),
			balance: 500,
			guest: false,
			loadout: viewer,
			...handlers,
			...props
		}
	});
	return { ...handlers, ...utils };
}

function previewSkin(container: HTMLElement): string | null | undefined {
	return container.querySelector<HTMLElement>(".detail-art")?.dataset.previewSkin;
}

afterEach(cleanup);

describe("PaletteDetail", () => {
	it("opens on the first color for sale and previews it", () => {
		const { container } = renderDetail();

		expect(screen.getByRole("heading", { name: "Hot" })).toBeInTheDocument();
		expect(screen.getByText("250")).toBeInTheDocument();
		expect(previewSkin(container)).toBe("uni:neon:hot");
	});

	it("tries the clicked color and blocks a buy the balance cannot cover", async () => {
		const { container, ontry } = renderDetail();

		await fireEvent.click(container.querySelector('[data-swatch-id="uni:neon:volt"]')!);

		expect(ontry).toHaveBeenCalledWith("uni:neon:volt");
		expect(screen.getByRole("heading", { name: "Volt" })).toBeInTheDocument();
		expect(previewSkin(container)).toBe("uni:neon:volt");
		expect(screen.getByRole("button", { name: "BUY" })).toBeDisabled();
		expect(screen.getByText("Not enough cash")).toBeInTheDocument();
	});

	it("disables BUY for a color that is not sold alone", async () => {
		const { container } = renderDetail();

		await fireEvent.click(container.querySelector('[data-swatch-id="uni:neon:dim"]')!);

		expect(screen.getByRole("button", { name: "BUY" })).toBeDisabled();
		expect(screen.getByText("This color is not sold alone.")).toBeInTheDocument();
	});

	it("opens on the first color not owned and says when a color is owned", async () => {
		const { container } = renderDetail({ owned: new Set(["uni:neon:hot"]) });

		expect(screen.getByRole("heading", { name: "Volt" })).toBeInTheDocument();

		await fireEvent.click(container.querySelector('[data-swatch-id="uni:neon:hot"]')!);

		expect(screen.getByText("You already own this.")).toBeInTheDocument();
	});

	it("shows Register to buy for a guest and never calls onbuy", async () => {
		const { onbuy } = renderDetail({ guest: true });
		const buy = screen.getByRole("button", { name: "Register to buy" });
		expect(buy).toBeDisabled();

		await fireEvent.click(buy);
		expect(onbuy).not.toHaveBeenCalled();
	});

	it("buys the chosen color and closes", async () => {
		const { onbuy, onclose } = renderDetail();

		await fireEvent.click(screen.getByRole("button", { name: "BUY" }));
		expect(onbuy).toHaveBeenCalledWith(expect.objectContaining({ id: "uni:neon:hot" }));

		await fireEvent.click(screen.getByRole("button", { name: "Close" }));
		expect(onclose).toHaveBeenCalledTimes(1);
	});

	it("shows a failure reason", () => {
		renderDetail({ error: "insufficient_cash" });

		expect(screen.getByRole("alert")).toHaveTextContent("Not enough cash.");
	});
});
