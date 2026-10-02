import { describe, it, expect } from "vitest";
import {
	getCardDescription,
	getCardTitle,
	getCardInfo,
	cardInfoByKind,
	cardValueFromKind
} from "$lib/glossary/cardDescriptions";
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
		expect(skipDesc).toContain("[k=vanilla:skip]");
		expect(skipDesc).toContain("[k=vanilla:turn]");

		const draw2Desc = getCardDescription("+2", "en");
		expect(draw2Desc).toContain("[k=vanilla:draw]");
		expect(draw2Desc).toContain("[k=vanilla:draw_pile]");

		const wildDesc = getCardDescription("jolly", "en");
		expect(wildDesc).toContain("[k=vanilla:wild]");
		expect(wildDesc).toContain("[k=vanilla:color]");
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

	it("maps a vanilla kind id back to a card value", () => {
		expect(cardValueFromKind("vanilla:red_draw2")).toBe("+2");
		expect(cardValueFromKind("vanilla:green_skip")).toBe("skip");
		expect(cardValueFromKind("vanilla:wild_draw4")).toBe("jolly_draw4");
		expect(cardValueFromKind("vanilla:blue_7")).toBe("7");
		expect(cardValueFromKind("mymod:thing")).toBeNull();
		expect(cardValueFromKind("vanilla:bogus")).toBeNull();
	});

	it("resolves card info from a kind id", () => {
		expect(cardInfoByKind("vanilla:red_draw2", "en")?.title).toBe(
			getCardTitle({ type: "red", value: "+2" }, "en")
		);
		expect(cardInfoByKind("mymod:thing", "en")).toBeNull();
	});

	it("gives a colour-free play title for a kind id", () => {
		expect(cardInfoByKind("vanilla:blue_7", "en")?.title).toBe("7");
		expect(cardInfoByKind("vanilla:red_draw2", "en")?.title).toBe("Draw Two (+2)");
	});
});
