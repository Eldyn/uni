import { describe, it, expect, beforeAll } from "vitest";
import { readFileSync } from "node:fs";
import { resolve } from "node:path";

let sheetText: string;

beforeAll(() => {
	sheetText = readFileSync(resolve(__dirname, "../../../app.css"), "utf8");
});

describe("elevation", () => {
	it("defines a three-step hard-offset ramp", () => {
		expect(sheetText).toMatch(/--elevation-1:\s*2px 2px 0/);
		expect(sheetText).toMatch(/--elevation-2:\s*4px 4px 0/);
		expect(sheetText).toMatch(/--elevation-3:\s*8px 8px 0/);
	});

	it("removes the soft-shadow tokens they replace", () => {
		expect(sheetText).not.toMatch(/--shadow:/);
		expect(sheetText).not.toMatch(/--lowShadow:/);
		expect(sheetText).not.toMatch(/--lowShadowHover:/);
	});
});
