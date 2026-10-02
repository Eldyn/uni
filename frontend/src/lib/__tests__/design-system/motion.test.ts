import { describe, it, expect, beforeAll } from "vitest";
import { readFileSync } from "node:fs";
import { resolve } from "node:path";

let sheetText: string;

beforeAll(() => {
	sheetText = readFileSync(resolve(__dirname, "../../../app.css"), "utf8");
});

describe("motion tokens", () => {
	it("neutralises motion under prefers-reduced-motion", () => {
		expect(sheetText).toMatch(/@media\s*\(prefers-reduced-motion:\s*reduce\)/);
	});
});
