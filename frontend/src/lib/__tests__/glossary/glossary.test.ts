import { beforeEach, describe, expect, it } from "vitest";
import { getGlossaryEntry, hasGlossaryKeyword } from "$lib/glossary/glossary";
import { storeModGlossary } from "$lib/stores/modGlossary.svelte";
import { storeModTexts } from "$lib/stores/modTexts.svelte";
import { prepareCoreTexts } from "./coreTextsHarness";

describe("namespaced glossary", () => {
	beforeEach(async () => {
		await prepareCoreTexts("en");
	});

	it("resolves a namespaced mechanic", () => {
		expect(getGlossaryEntry("uni:turn", "en")?.title).toBe("Turn");
		expect(hasGlossaryKeyword("uni:turn")).toBe(true);
	});

	it("resolves a uni card kind through the automatic entry of the card", () => {
		const entry = getGlossaryEntry("uni:red_draw2", "en");
		expect(entry?.title).toBe("Draw Two (+2)");
		expect(entry?.description).toContain("[k=uni:draw]");
	});

	it("lets the glossary entry of wild replace the automatic entry of the wild card", () => {
		expect(getGlossaryEntry("uni:wild", "en")?.description).toContain(
			"The player chooses the active"
		);
	});

	it("returns null for an unknown keyword", () => {
		expect(getGlossaryEntry("uni:nope", "en")).toBeNull();
		expect(getGlossaryEntry("nope", "en")).toBeNull();
		expect(hasGlossaryKeyword("uni:nope")).toBe(false);
	});
});

describe("glossary before the uni texts arrive", () => {
	beforeEach(() => {
		storeModTexts.reset();
		storeModGlossary.reset();
	});

	it("gives null, never a raw key, with no listing at all", () => {
		expect(getGlossaryEntry("uni:turn", "en")).toBeNull();
		expect(hasGlossaryKeyword("uni:turn")).toBe(false);
	});
});
