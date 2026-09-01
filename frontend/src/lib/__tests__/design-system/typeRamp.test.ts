import { describe, it, expect, beforeAll } from "vitest";
import { readFileSync } from "node:fs";
import { resolve } from "node:path";

let sheetText: string;

beforeAll(() => {
	sheetText = readFileSync(resolve(__dirname, "../../../app.css"), "utf8");
});

describe("fluid type ramp", () => {
	it.each(["--text-hero", "--text-display", "--text-title", "--text-body", "--text-small"])(
		"%s is a clamp() expression",
		(token) => {
			const match = sheetText.match(new RegExp(`${token}:\\s*(clamp\\([^;]*\\));`));
			expect(match, `expected ${token} to be a clamp()`).toBeTruthy();
		}
	);

	it("title-hero no longer uses a fixed 10rem size", () => {
		const titleHeroBlock = sheetText.slice(
			sheetText.indexOf(".title-hero {"),
			sheetText.indexOf(".title-hero {") + 200
		);
		expect(titleHeroBlock).not.toMatch(/font-size:\s*10rem/);
		expect(titleHeroBlock).toMatch(/font-size:\s*var\(--text-hero\)/);
	});
});
