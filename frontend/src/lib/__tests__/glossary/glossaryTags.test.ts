import { describe, it, expect } from "vitest";
import { getGlossaryEntry } from "$lib/glossary/glossary";

describe("Glossary tags", () => {
	it("returns tags for registered keywords", () => {
		const turnEntry = getGlossaryEntry("uni:turn", "en");
		expect(turnEntry).not.toBeNull();
		expect(turnEntry?.tags).toBeDefined();
		expect(turnEntry!.tags!.length).toBeGreaterThan(0);
		expect(turnEntry!.tags![0].label).toBe("Flow");
		expect(turnEntry!.tags![0].bg).toBeDefined();

		const sevenZeroEntry = getGlossaryEntry("uni:seven_zero", "en");
		expect(sevenZeroEntry?.tags?.[0].label).toBe("Rule");

		const playEntry = getGlossaryEntry("uni:play", "en");
		expect(playEntry?.tags?.[0].label).toBe("Action");
	});
});
