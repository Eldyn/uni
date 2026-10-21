// Every price and balance shows the Blank icon, never the old gold coin dot.
import { describe, it, expect } from "vitest";
import itemCard from "../../components/shop/ItemCard.svelte?raw";
import swatchCard from "../../components/shop/PaletteCard.svelte?raw";
import featuredBanner from "../../components/shop/FeaturedBanner.svelte?raw";
import itemDetail from "../../components/shop/ItemDetail.svelte?raw";
import swatchDetail from "../../components/shop/PaletteDetail.svelte?raw";
import shopHero from "../../components/shop/ShopHero.svelte?raw";
import marketplaceCard from "../../components/shop/MarketplaceCard.svelte?raw";
import matchEarningsLine from "../../components/game/MatchEarningsLine.svelte?raw";

const SOURCES: Record<string, string> = {
	ItemCard: itemCard,
	SwatchCard: swatchCard,
	FeaturedBanner: featuredBanner,
	ItemDetail: itemDetail,
	SwatchDetail: swatchDetail,
	ShopHero: shopHero,
	MarketplaceCard: marketplaceCard,
	MatchEarningsLine: matchEarningsLine
};

describe("Blank icon use", () => {
	it.each(Object.entries(SOURCES))("%s uses BlankIcon and no coin", (_name, source) => {
		expect(source).toContain("<BlankIcon");
		expect(source).not.toMatch(/class="coin"/);
		expect(source).not.toMatch(/\.coin\s*\{/);
	});
});
