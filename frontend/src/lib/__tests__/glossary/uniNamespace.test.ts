import { describe, expect, it } from "vitest";
import { getGlossaryEntry } from "$lib/glossary/glossary";

const messageFiles = import.meta.glob<string>("../../../../messages/*.json", {
	query: "?raw",
	import: "default",
	eager: true
});

describe("uni namespace in glossary keywords", () => {
	it("resolves a uni keyword to an entry that keeps the uni id", () => {
		expect(getGlossaryEntry("uni:draw")?.id).toBe("uni:draw");
	});

	it("does not resolve a vanilla keyword", () => {
		expect(getGlossaryEntry("vanilla:draw")).toBeNull();
	});

	it("holds no vanilla keyword in any message file", () => {
		const offenders = Object.entries(messageFiles)
			.filter(([, content]) => content.includes("[k=vanilla:"))
			.map(([path]) => path);
		expect(Object.keys(messageFiles).length).toBeGreaterThan(0);
		expect(offenders).toEqual([]);
	});
});
