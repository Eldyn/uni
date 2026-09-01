import { describe, it, expect, beforeAll } from "vitest";
import { readFileSync } from "node:fs";
import { resolve } from "node:path";

let sheetText: string;

beforeAll(() => {
	sheetText = readFileSync(resolve(__dirname, "../../../app.css"), "utf8");
});

describe("pixel-corner notch consolidation", () => {
	it("defines one shared --notch-clip custom property", () => {
		expect(sheetText).toMatch(/--notch-clip:\s*polygon\(/);
	});

	it("all three notch consumers reference the shared property instead of repeating the polygon", () => {
		const clipPathDeclarations = sheetText.match(/clip-path:\s*polygon\([^;]*\);/g) ?? [];
		// Once consolidated, no rule should inline a literal polygon(...) —
		// they should all read `clip-path: var(--notch-clip);` instead.
		expect(clipPathDeclarations).toHaveLength(0);

		const varUsages = sheetText.match(/clip-path:\s*var\(--notch-clip\);/g) ?? [];
		expect(varUsages.length).toBeGreaterThanOrEqual(3);
	});
});
