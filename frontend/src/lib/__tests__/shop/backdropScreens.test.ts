import { describe, it, expect } from "vitest";
import shopSource from "../../components/shop/ShopScreen.svelte?raw";
import wardrobeSource from "../../components/wardrobe/WardrobeScreen.svelte?raw";
import backdropSource from "../../components/shop/ShopBackdrop.svelte?raw";

describe("backdrop screens", () => {
	it("fill the scroll area with the flat hint color", () => {
		expect(shopSource).toContain(
			'<div class="shop-screen" style:background-color={HOME_BACKGROUND_HINT_CSS}>'
		);
		expect(wardrobeSource).toContain(
			'<div class="wardrobe-screen" style:background-color={HOME_BACKGROUND_HINT_CSS}>'
		);
	});

	it("add no gradient or pseudo-element overlay", () => {
		for (const source of [shopSource, wardrobeSource, backdropSource]) {
			expect(source).not.toMatch(/linear-gradient|radial-gradient|::before|::after/);
		}
	});
});
