import { describe, it, expect } from "vitest";
import { readFileSync } from "node:fs";
import { resolve } from "node:path";

describe("TextEffects undulate geometry", () => {
	it("char-wrap keeps characters on a fixed baseline instead of stretching to the flex cross-axis", () => {
		const source = readFileSync(
			resolve(__dirname, "../../components/common/TextEffects.svelte"),
			"utf8"
		);
		const charWrapRule = source.match(/\.char-wrap\s*\{([^}]*)\}/);
		expect(charWrapRule, "expected a .char-wrap rule").toBeTruthy();
		// `align-items: stretch` (the flex default) is never acceptable here: it
		// would let a single-glyph flex item resize to the container's cross-size.
		expect(charWrapRule![1]).not.toMatch(/align-items:\s*stretch/);
		// `baseline` keeps every animated character sitting on the same text
		// baseline regardless of the wave's current per-character offset.
		expect(charWrapRule![1]).toMatch(/align-items:\s*baseline/);
	});
});
