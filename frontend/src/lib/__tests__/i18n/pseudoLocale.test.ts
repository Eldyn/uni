import { describe, it, expect } from "vitest";
import en from "../../../../messages/en.json";
import pseudo from "../../../../messages/pseudo.json";

describe("pseudo locale", () => {
	it("has an entry for every English key", () => {
		for (const key of Object.keys(en)) {
			if (key === "$schema") continue;
			expect(pseudo, `missing pseudo entry for "${key}"`).toHaveProperty(key);
		}
	});

	it("every pseudo string is at least 30% longer than its English source, to surface overflow", () => {
		for (const [key, value] of Object.entries(en)) {
			if (key === "$schema" || typeof value !== "string") continue;
			const pseudoValue = (pseudo as Record<string, string>)[key];
			expect(
				pseudoValue.length,
				`"${key}" pseudo string is not padded enough to catch overflow`
			).toBeGreaterThanOrEqual(Math.ceil(value.length * 1.3));
		}
	});
});
