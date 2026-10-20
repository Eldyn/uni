import { describe, expect, it } from "vitest";
import slotPickerSource from "../../components/wardrobe/SlotPicker.svelte?raw";

/** The declarations of one CSS rule, read straight from the component source. */
function cssBlock(source: string, selector: string): string {
	const escaped = selector.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
	const match = source.match(new RegExp(`${escaped}\\s*\\{([^}]*)\\}`));
	if (!match) throw new Error(`rule not found: ${selector}`);
	return match[1];
}

describe("SlotPicker art tile CSS", () => {
	it("keeps the art square after a live resize", () => {
		const tile = cssBlock(slotPickerSource, ".slot-art-tile");
		expect(tile).toMatch(/aspect-ratio:\s*1/);
		expect(tile).toMatch(/grid-template-columns:\s*minmax\(0,\s*1fr\)/);
		expect(tile).toMatch(/overflow:\s*hidden/);
	});
});
