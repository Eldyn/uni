import { describe, it, expect, beforeAll } from "vitest";
import { readFileSync } from "node:fs";
import { resolve } from "node:path";

let sheetText: string;

beforeAll(() => {
	sheetText = readFileSync(resolve(__dirname, "../../../app.css"), "utf8");
});

describe("motion tokens", () => {
	it("defines duration and easing custom properties", () => {
		expect(sheetText).toMatch(/--duration-fast:\s*120ms/);
		expect(sheetText).toMatch(/--duration-base:\s*200ms/);
		expect(sheetText).toMatch(/--duration-slow:\s*320ms/);
		expect(sheetText).toMatch(/--ease-step:\s*steps\(/);
	});

	it("neutralises motion under prefers-reduced-motion", () => {
		expect(sheetText).toMatch(/@media\s*\(prefers-reduced-motion:\s*reduce\)/);
	});
});
