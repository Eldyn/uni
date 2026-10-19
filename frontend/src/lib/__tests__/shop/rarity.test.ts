import { describe, it, expect } from "vitest";
import { RARITY_COLOURS, rarityLabelColour, rarityLabelKey } from "$lib/shop/rarity";
import type { Rarity } from "$lib/avatar/types";

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
