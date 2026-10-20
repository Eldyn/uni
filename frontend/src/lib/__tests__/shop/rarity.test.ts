import { describe, it, expect } from "vitest";
import {
	RARITY_COLOURS,
	compareRarity,
	paletteRarity,
	rarityLabelColour,
	rarityLabelKey
} from "$lib/shop/rarity";
import type { Palette, Rarity } from "$lib/avatar/types";

describe("RARITY_COLOURS", () => {
	it("has the four rarity hex values", () => {
		expect(RARITY_COLOURS).toEqual({
			basic: "#2c354d",
			common: "#9ca3af",
			rare: "#4ade80",
			epic: "#60a5fa",
			special: "#c084fc"
		});
	});
});

describe("rarityLabelColour", () => {
	it("uses the text color for basic and the rarity color otherwise", () => {
		expect(rarityLabelColour("basic")).toBe("var(--text)");
		expect(rarityLabelColour("special")).toBe("#c084fc");
	});
});

describe("compareRarity", () => {
	it("orders basic, common, rare, epic, special", () => {
		const shuffled: Rarity[] = ["special", "rare", "basic", "epic", "common"];
		expect([...shuffled].sort(compareRarity)).toEqual([
			"basic",
			"common",
			"rare",
			"epic",
			"special"
		]);
	});
});

describe("rarityLabelKey", () => {
	it("maps each rarity to its paraglide message key", () => {
		expect(rarityLabelKey("common")).toBe("shop_rarity_common");
		expect(rarityLabelKey("rare")).toBe("shop_rarity_rare");
		expect(rarityLabelKey("epic")).toBe("shop_rarity_epic");
	});

	it("covers every rarity", () => {
		const rarities: Rarity[] = ["basic", "common", "rare", "epic", "special"];
		for (const rarity of rarities) {
			expect(rarityLabelKey(rarity)).toBe(`shop_rarity_${rarity}`);
		}
	});
});

describe("paletteRarity", () => {
	it("makes a starter palette basic", () => {
		const starter: Palette = {
			id: "basic",
			name: "Basic",
			price: 0,
			starter: true,
			swatches: [{ id: "basic:red", hex: "#e84a4a" }]
		};
		expect(paletteRarity(starter)).toBe("basic");
	});

	it("takes the highest swatch rarity", () => {
		const mixed: Palette = {
			id: "uni:neon",
			name: "Neon",
			price: 250,
			swatches: [
				{ id: "uni:neon:hot", hex: "#ff2d95", rarity: "rare" },
				{ id: "uni:neon:volt", hex: "#00e5ff", rarity: "epic" },
				{ id: "uni:neon:dim", hex: "#333333" }
			]
		};
		expect(paletteRarity(mixed)).toBe("epic");
	});

	it("falls back to common when no swatch is rated", () => {
		const plain: Palette = {
			id: "uni:plain",
			name: "Plain",
			price: 100,
			swatches: [{ id: "uni:plain:grey", hex: "#9ca3af" }]
		};
		expect(paletteRarity(plain)).toBe("common");
	});
});
