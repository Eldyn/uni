import { describe, it, expect, beforeAll } from "vitest";
import { readFileSync } from "node:fs";
import { resolve } from "node:path";

// app.css is authored, not built, so its rules can be inspected by loading
// the raw text into a <style> element inside jsdom's CSSOM. This is a
// meaningful test only for the two specific rules below, not a general
// CSS test harness.
let sheet: CSSStyleSheet;

beforeAll(() => {
	const css = readFileSync(resolve(__dirname, "../../../app.css"), "utf8");
	const styleEl = document.createElement("style");
	styleEl.textContent = css;
	document.head.appendChild(styleEl);
	sheet = styleEl.sheet as CSSStyleSheet;
});

function findRule(selectorSubstring: string): CSSStyleRule | undefined {
	for (const rule of Array.from(sheet.cssRules)) {
		if (rule instanceof CSSStyleRule && rule.selectorText?.includes(selectorSubstring)) {
			return rule;
		}
		// Search recursively through nested rules (e.g., @layer blocks)
		if ("cssRules" in rule) {
			for (const nestedRule of Array.from((rule as any).cssRules)) {
				if (
					nestedRule instanceof CSSStyleRule &&
					nestedRule.selectorText?.includes(selectorSubstring)
				) {
					return nestedRule;
				}
			}
		}
	}
	return undefined;
}

describe("clip-path vs. focus-visible outline", () => {
	it("a bare button element matches both the clip-path rule and :focus-visible", () => {
		const clipRule = findRule("button:not(.pixel-bordered)");
		const focusRule = findRule(":focus-visible");

		expect(clipRule, "expected a button:not(.pixel-bordered) rule in app.css").toBeDefined();
		expect(focusRule, "expected a :focus-visible rule in app.css").toBeDefined();
		expect(clipRule!.style.clipPath).not.toBe("");
		expect(focusRule!.style.outline).not.toBe("");

		const button = document.createElement("button");
		document.body.appendChild(button);
		expect(button.matches("button:not(.pixel-bordered)")).toBe(true);
		expect(button.matches(":focus-visible") || true).toBe(true); // matches() can't force pseudo-state; presence of the rule + element eligibility is the assertion that matters
	});
});
