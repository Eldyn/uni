import { describe, it, expect } from "vitest";
import { getCardDescription, getCardTitle, getCardInfo } from "$lib/glossary/cardDescriptions";
import { ValueMap } from "$lib/generated/schemas";
import type { CardValue } from "$stores/game.svelte";

describe("cardDescriptions", () => {
	it("returns valid descriptions for all card values in ValueMap", () => {
		for (const val of ValueMap) {
			const descEn = getCardDescription(val as CardValue, "en");
			expect(descEn).toBeTruthy();
			expect(descEn.length).toBeGreaterThan(10);

			const descIt = getCardDescription(val as CardValue, "it");
			expect(descIt).toBeTruthy();
			expect(descIt.length).toBeGreaterThan(10);
		}
	});

	it("contains keyword markup in action card descriptions", () => {
		const skipDesc = getCardDescription("skip", "en");
		expect(skipDesc).toContain("[k=skip]");
		expect(skipDesc).toContain("[k=turn]");

		const draw2Desc = getCardDescription("+2", "en");
		expect(draw2Desc).toContain("[k=draw]");
		expect(draw2Desc).toContain("[k=draw_pile]");

		const wildDesc = getCardDescription("jolly", "en");
		expect(wildDesc).toContain("[k=wild]");
		expect(wildDesc).toContain("[k=color]");
	});

	it("returns formatted title for colored and wild cards", () => {
		const redSkip = getCardInfo({ type: "red", value: "skip" as CardValue }, "en");
		expect(redSkip.title).toBe("Skip");

		const blue5 = getCardInfo({ type: "blue", value: "5" as CardValue }, "en");
		expect(blue5.title).toBe("Blue 5");

		const wild = getCardInfo({ type: "black", value: "jolly" as CardValue }, "en");
		expect(wild.title).toBe("Wild");

		const wild4 = getCardInfo({ type: "black", value: "jolly_draw4" as CardValue }, "en");
		expect(wild4.title).toBe("Wild Draw Four (+4)");
	});
});
