import { describe, it, expect, beforeAll } from "vitest";
import { readFileSync } from "node:fs";
import { resolve } from "node:path";

function loadRootStyle(): CSSStyleDeclaration {
	const css = readFileSync(resolve(__dirname, "../../../app.css"), "utf8");
	const styleEl = document.createElement("style");
	styleEl.textContent = css;
	document.head.appendChild(styleEl);

	const probe = document.createElement("div");
	document.documentElement.appendChild(probe);
	return getComputedStyle(probe);
}

let root: CSSStyleDeclaration;

beforeAll(() => {
	root = loadRootStyle();
});

describe("spacing scale", () => {
	it.each([
		["--space-1", "4px"],
		["--space-2", "8px"],
		["--space-3", "12px"],
		["--space-4", "16px"],
		["--space-6", "24px"],
		["--space-8", "32px"],
		["--space-12", "48px"],
		["--space-16", "64px"]
	])("%s resolves to %s", (token, expected) => {
		expect(root.getPropertyValue(token).trim()).toBe(expected);
	});
});
