import { describe, expect, it } from "vitest";
import { getGlossaryEntry, hasGlossaryKeyword } from "$lib/glossary/glossary";

describe("namespaced glossary", () => {
	it("resolves a namespaced mechanic", () => {
		expect(getGlossaryEntry("vanilla:turn", "en")?.title).toBeTruthy();
		expect(hasGlossaryKeyword("vanilla:turn")).toBe(true);
	});

	it("resolves a vanilla card kind from the card info index", () => {
		const entry = getGlossaryEntry("vanilla:red_draw2", "en");
		expect(entry?.title).toBeTruthy();
		expect(entry?.description).toContain("[k=draw]");
	});

	it("returns null for an unknown keyword", () => {
		expect(getGlossaryEntry("vanilla:nope", "en")).toBeNull();
		expect(getGlossaryEntry("nope", "en")).toBeNull();
		expect(hasGlossaryKeyword("vanilla:nope")).toBe(false);
	});
});
