import { beforeEach, describe, it, expect } from "vitest";
import { storeModTexts } from "$lib/stores/modTexts.svelte";
import { storeModGlossary } from "$lib/stores/modGlossary.svelte";
import { prepareCoreTexts } from "./coreTextsHarness";
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
	beforeEach(async () => {
		await prepareCoreTexts("en");
	});

	it("returns valid descriptions for all card values in ValueMap", async () => {
		for (const locale of ["en", "it"] as const) {
			await prepareCoreTexts(locale);
			for (const val of ValueMap) {
				const description = getCardDescription(val as CardValue);
				expect(description, `${locale} ${val}`).toBeTruthy();
				expect(description!.length).toBeGreaterThan(10);
			}
		}
	});

	it("contains keyword markup in action card descriptions", () => {
		const skipDesc = getCardDescription("skip");
		expect(skipDesc).toContain("[k=uni:skip]");
		expect(skipDesc).toContain("[k=uni:turn]");

		const draw2Desc = getCardDescription("+2");
		expect(draw2Desc).toContain("[k=uni:draw]");
		expect(draw2Desc).toContain("[k=uni:draw_pile]");

		const wildDesc = getCardDescription("jolly");
		expect(wildDesc).toContain("[k=uni:wild]");
		expect(wildDesc).toContain("[k=uni:color]");
	});

	it("returns formatted title for colored and wild cards", () => {
		const redSkip = getCardInfo({ type: "red", value: "skip" as CardValue });
		expect(redSkip?.title).toBe("Skip");

		const blue5 = getCardInfo({ type: "blue", value: "5" as CardValue });
		expect(blue5?.title).toBe("Blue 5");

		const wild = getCardInfo({ type: "black", value: "jolly" as CardValue });
		expect(wild?.title).toBe("Wild");

		const wild4 = getCardInfo({ type: "black", value: "jolly_draw4" as CardValue });
		expect(wild4?.title).toBe("Wild Draw Four (+4)");
	});

	it("maps a uni kind id back to a card value", () => {
		expect(cardValueFromKind("uni:red_draw2")).toBe("+2");
		expect(cardValueFromKind("uni:green_skip")).toBe("skip");
		expect(cardValueFromKind("uni:wild_draw4")).toBe("jolly_draw4");
		expect(cardValueFromKind("uni:blue_7")).toBe("7");
		expect(cardValueFromKind("mymod:thing")).toBeNull();
		expect(cardValueFromKind("uni:bogus")).toBeNull();
	});

	it("resolves card info from a kind id", () => {
		expect(cardInfoByKind("uni:red_draw2")?.title).toBe(getCardTitle({ type: "red", value: "+2" }));
		expect(cardInfoByKind("mymod:thing")).toBeNull();
	});

	it("gives a colour-free play title for a kind id", () => {
		expect(cardInfoByKind("uni:blue_7")?.title).toBe("7");
		expect(cardInfoByKind("uni:red_draw2")?.title).toBe("Draw Two (+2)");
	});
});

describe("cardValueFromKind under the uni namespace", () => {
	beforeEach(async () => {
		await prepareCoreTexts("en");
	});

	it("reads a uni kind", () => {
		expect(cardValueFromKind("uni:red_5")).toBe("5");
		expect(cardValueFromKind("uni:wild_draw4")).toBe("jolly_draw4");
		expect(cardInfoByKind("uni:red_draw2")?.title).toBeTruthy();
	});

	it("still rejects other namespaces", () => {
		expect(cardValueFromKind("seven_zero:red_5")).toBeNull();
	});
});

describe("card texts before the uni texts arrive", () => {
	beforeEach(() => {
		storeModTexts.reset();
		storeModGlossary.reset();
	});

	it("gives null, never a raw key", () => {
		expect(getCardDescription("skip")).toBeNull();
		expect(getCardTitle({ type: "red", value: "skip" as CardValue })).toBeNull();
		expect(getCardInfo({ type: "red", value: "skip" as CardValue })).toBeNull();
		expect(cardInfoByKind("uni:red_skip")).toBeNull();
	});

	it("keeps the colour title of a number card, which needs no text", () => {
		expect(getCardTitle({ type: "blue", value: "5" as CardValue })).toBe("Blue 5");
	});
});
