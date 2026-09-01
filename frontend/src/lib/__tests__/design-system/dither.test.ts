import { describe, it, expect, beforeAll } from "vitest";
import { readFileSync } from "node:fs";
import { resolve } from "node:path";

let sheetText: string;

beforeAll(() => {
	sheetText = readFileSync(resolve(__dirname, "../../../app.css"), "utf8");
});

describe("dither family", () => {
	it.each([".dither-2", ".dither-4", ".dither-8"])("defines %s", (className) => {
		expect(sheetText).toContain(`${className} {`);
	});

	it("defines a blur-backed veil variant", () => {
		expect(sheetText).toMatch(/\.dither-veil\s*\{[^}]*backdrop-filter:\s*blur/s);
	});

	it("uses repeating-conic-gradient, never an image or canvas", () => {
		const ditherBlock = sheetText.slice(
			sheetText.indexOf(".dither-2"),
			sheetText.indexOf(".dither-veil") + 200
		);
		expect(ditherBlock).toMatch(/repeating-conic-gradient/);
		expect(ditherBlock).not.toMatch(/url\(/);
	});
});
