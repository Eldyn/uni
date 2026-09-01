import { describe, it, expect, beforeAll } from "vitest";
import { readFileSync } from "node:fs";
import { resolve } from "node:path";

let sheetText: string;

beforeAll(() => {
	sheetText = readFileSync(resolve(__dirname, "../../../app.css"), "utf8");
});

describe("button primitive variants", () => {
	it.each([".btn-secondary", ".btn-ghost", ".btn-danger", ".btn-sm", ".btn-md", ".btn-lg"])(
		"defines %s",
		(className) => {
			expect(sheetText).toContain(`${className} {`);
		}
	);

	it("every size variant meets the 44px minimum touch target", () => {
		for (const size of [".btn-sm", ".btn-md", ".btn-lg"]) {
			const match = sheetText.match(new RegExp(`${size.replace(".", "\\.")}\\s*\\{([^}]*)\\}`));
			expect(match, `expected a rule for ${size}`).toBeTruthy();
			expect(match![1]).toMatch(/min-height:\s*44px/);
		}
	});

	it("btn-danger uses the danger token, not a hardcoded colour", () => {
		const match = sheetText.match(/\.btn-danger\s*\{([^}]*)\}/);
		expect(match![1]).toMatch(/var\(--danger\)/);
	});
});
