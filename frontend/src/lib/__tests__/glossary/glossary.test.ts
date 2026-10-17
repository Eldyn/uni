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
		expect(entry?.description).toContain("[k=vanilla:draw]");
	});

	it("resolves every rule mod entry in each locale", () => {
		for (const locale of ["en", "de", "es", "it", "ja", "ko", "ru", "uk", "zh"]) {
			for (const rule of [
				"seven_zero",
				"draw_stacking",
				"force_play",
				"jump_in",
				"progressive",
				"no_bluffing"
			]) {
				const entry = getGlossaryEntry(`vanilla:${rule}`, locale);
				expect(entry?.title, `${rule} title in ${locale}`).toBeTruthy();
				expect(entry?.description, `${rule} description in ${locale}`).toBeTruthy();
			}
		}
	});

	it("describes progressive as draw until playable, then choose", () => {
		expect(getGlossaryEntry("vanilla:progressive", "en")?.description).toContain(
			"choose whether to play it or keep it"
		);
	});

	it("returns null for an unknown keyword", () => {
		expect(getGlossaryEntry("vanilla:nope", "en")).toBeNull();
		expect(getGlossaryEntry("nope", "en")).toBeNull();
		expect(hasGlossaryKeyword("vanilla:nope")).toBe(false);
	});
});
