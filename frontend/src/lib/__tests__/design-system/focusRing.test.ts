import { describe, it, expect, beforeAll } from "vitest";
import { readFileSync } from "node:fs";
import { resolve } from "node:path";

let sheetText: string;

beforeAll(() => {
	sheetText = readFileSync(resolve(__dirname, "../../../app.css"), "utf8");
});

describe("focus ring on clipped buttons", () => {
	it("gives button:not(.pixel-bordered) a focus-visible rule using box-shadow, not outline", () => {
		const match = sheetText.match(/button:not\(\.pixel-bordered\):focus-visible\s*\{([^}]*)\}/);
		expect(match, "expected a button:not(.pixel-bordered):focus-visible rule").toBeTruthy();
		const body = match![1];
		expect(body).toMatch(/box-shadow:\s*inset/);
		// Normalise whitespace so `outline:none` and `outline: none` behave the
		// same — the naive `\s*(?!none)` wrongly matches the spaced form.
		expect(body.replace(/\s+/g, "")).not.toMatch(/outline:(?!none)/);
	});

	it("suppresses the default outline on clipped buttons so it cannot double up with the inset ring", () => {
		const match = sheetText.match(/button:not\(\.pixel-bordered\):focus-visible\s*\{([^}]*)\}/);
		expect(match![1]).toMatch(/outline:\s*none/);
	});
});
