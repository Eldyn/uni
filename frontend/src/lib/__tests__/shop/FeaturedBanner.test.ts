// The featured hero: the preview must never show the raw mask file. When the
// avatar bake is ready it shows the baked sprite; otherwise it shows an empty
// stage tile. BUY is primary, TRY ON secondary.

import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { cleanup, render, screen } from "@testing-library/svelte";
import type { CatalogueItem } from "$lib/avatar/types";

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

import FeaturedBanner from "$components/shop/FeaturedBanner.svelte";

function makeItem(overrides: Partial<CatalogueItem> = {}): CatalogueItem {
	return {
		id: "uni:hat_cap",
		slot: "head_top",
		name: "Cap",
		asset: { kind: "still", path: "head_top/uni_hat_cap.png", bob: [[0, 0]] as [number, number][] },
		regions: 1,
		rarity: "rare",
		price: 80,
		...overrides
	};
}

function renderBanner(item: CatalogueItem = makeItem()) {
	return render(FeaturedBanner, {
		props: {
			item,
			owned: false,
			balance: 1500,
			guest: false,
			loadout: null,
			onbuy: vi.fn(),
			ontry: vi.fn()
		}
	});
}

beforeEach(() => {
	bakedState.url = null;
});

afterEach(cleanup);

describe("FeaturedBanner", () => {
	it("shows an empty stage tile, never the raw mask, while the bake is pending", () => {
		const { container } = renderBanner();
		const tile = container.querySelector<HTMLElement>(".featured-item-art");
		expect(tile).toBeTruthy();
		expect(tile?.getAttribute("style") ?? "").not.toContain("background-image");
		expect(container.querySelector(".avatar-sprite")).toBeNull();
	});

	it("shows the baked avatar once the bake resolves", () => {
		bakedState.url = "blob:avatar";
		const { container } = renderBanner();
		expect(container.querySelector(".avatar-sprite")).toBeTruthy();
		expect(container.querySelector(".featured-item-art")).toBeNull();
	});

	it("styles BUY as primary and TRY ON as secondary", () => {
		renderBanner();
		expect(screen.getByRole("button", { name: "BUY" })).toHaveClass("btn");
		expect(screen.getByRole("button", { name: "TRY ON" })).toHaveClass("btn-secondary");
	});
});
