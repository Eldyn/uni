// The featured shop item shows its tag, name, rarity and price in Pypx thick
// (owner ruling). The item detail drawer keeps the heading font.

import { describe, it, expect } from "vitest";
import bannerSource from "../../components/shop/FeaturedBanner.svelte?raw";
import detailSource from "../../components/shop/ItemDetail.svelte?raw";

function cssBlock(source: string, selector: string): string {
	const escaped = selector.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
	const match = source.match(new RegExp(`${escaped}\\s*\\{([^}]*)\\}`));
	if (!match) throw new Error(`rule not found: ${selector}`);
	return match[1];
}

const FEATURED_TEXT_SELECTORS = [
	".featured-tag",
	".featured-name",
	".featured-rarity",
	".featured-price"
];

describe("featured banner font", () => {
	it.each(FEATURED_TEXT_SELECTORS)("%s uses Pypx thick", (selector) => {
		const block = cssBlock(bannerSource, selector);
		expect(block).toMatch(/font-family:\s*var\(--pypx\)/);
		expect(block).toMatch(/font-weight:\s*800/);
	});

	it("drops the font utility classes from the four text elements", () => {
		expect(bannerSource).toMatch(/class="featured-tag"/);
		expect(bannerSource).toMatch(/class="featured-name"/);
		expect(bannerSource).toMatch(/class="featured-rarity"/);
		expect(bannerSource).toMatch(/class="featured-price"/);
	});

	it("leaves the item detail drawer name in the heading font", () => {
		expect(detailSource).toMatch(/class="detail-name font-heading"/);
	});
});
