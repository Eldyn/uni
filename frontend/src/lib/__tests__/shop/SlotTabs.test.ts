// The slot tab row of the Shop and the Wardrobe: every tab is a pixel-bordered
// button. The border comes from the pixel-bordered pseudo elements, so the tab
// CSS must not draw its own border or turn off the clip.

import { afterEach, describe, expect, it, vi } from "vitest";
import { cleanup, render, screen } from "@testing-library/svelte";
import SlotTabs from "$components/shop/SlotTabs.svelte";
import tabsSource from "../../components/shop/SlotTabs.svelte?raw";

function cssBlock(source: string, selector: string): string {
	const escaped = selector.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
	const match = source.match(new RegExp(`${escaped}\\s*\\{([^}]*)\\}`));
	if (!match) throw new Error(`rule not found: ${selector}`);
	return match[1];
}

const TABS = [
	{ id: "all", label: "All" },
	{ id: "head_top", label: "Hat" },
	{ id: "eyes", label: "Eyes" }
];

afterEach(cleanup);

describe("SlotTabs", () => {
	it("draws every tab with the pixel border", () => {
		render(SlotTabs, {
			props: { tabs: TABS, active: "all", onselect: vi.fn(), label: "Shop categories" }
		});

		const tabs = screen.getAllByRole("tab");
		expect(tabs).toHaveLength(3);
		for (const tab of tabs) {
			expect(tab).toHaveClass("pixel-bordered");
		}
	});

	it("keeps the selected state on the active tab", () => {
		render(SlotTabs, {
			props: { tabs: TABS, active: "eyes", onselect: vi.fn(), label: "Shop categories" }
		});

		expect(screen.getByRole("tab", { name: "Eyes" })).toHaveAttribute("aria-selected", "true");
		expect(screen.getByRole("tab", { name: "All" })).toHaveAttribute("aria-selected", "false");
	});

	it("colors the border through the pixel border variables only", () => {
		const tab = cssBlock(tabsSource, ".slot-tab");
		expect(tab).not.toMatch(/(^|[^-])border\s*:/m);
		expect(tab).not.toMatch(/clip-path/);
		expect(tab).toMatch(/--pc-fill:\s*var\(--surface\)/);
		expect(cssBlock(tabsSource, '.slot-tab[aria-selected="true"]')).toMatch(
			/--pc-border:\s*var\(--accent\)/
		);
	});
});
