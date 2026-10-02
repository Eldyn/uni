import { describe, it, expect, beforeAll } from "vitest";
import { readFileSync } from "node:fs";
import { resolve } from "node:path";

let sheetText: string;

beforeAll(() => {
	sheetText = readFileSync(resolve(__dirname, "../../../app.css"), "utf8");
});

describe("field primitive", () => {
	it("reserves height for the error slot so its appearance does not shift layout", () => {
		const match = sheetText.match(/\.field-error\s*\{([^}]*)\}/);
		expect(match![1]).toMatch(/min-height:/);
	});
});
