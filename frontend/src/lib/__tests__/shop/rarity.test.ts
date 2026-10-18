import { describe, it, expect } from "vitest";
import { RARITY_COLOURS, rarityLabelKey } from "$lib/shop/rarity";
import type { Rarity } from "$lib/avatar/types";

describe("RARITY_COLOURS", () => {
	it("has the four rarity hex values", () => {
		expect(RARITY_COLOURS).toEqual({
			common: "#9ca3af",
			uncommon: "#4ade80",
			rare: "#60a5fa",
			epic: "#ffcc00"
		});
	});
});

describe("rarityLabelKey", () => {
	it("maps each rarity to its paraglide message key", () => {
		expect(rarityLabelKey("common")).toBe("shop_rarity_common");
		expect(rarityLabelKey("uncommon")).toBe("shop_rarity_uncommon");
		expect(rarityLabelKey("rare")).toBe("shop_rarity_rare");
		expect(rarityLabelKey("epic")).toBe("shop_rarity_epic");
	});

	it("covers every rarity", () => {
		const rarities: Rarity[] = ["common", "uncommon", "rare", "epic"];
		for (const rarity of rarities) {
			expect(rarityLabelKey(rarity)).toBe(`shop_rarity_${rarity}`);
		}
	});
});
