import { describe, it, expect, beforeAll } from "vitest";
import { readFileSync } from "node:fs";
import { resolve } from "node:path";

let source: string;

beforeAll(() => {
	source = readFileSync(resolve(__dirname, "../../components/MainScreen.svelte"), "utf8");
});

describe("MainScreen scroll reveal", () => {
	it("uses animation-timeline: scroll(), not a scroll event listener", () => {
		expect(source).toMatch(/animation-timeline:\s*scroll\(/);
		expect(source).not.toMatch(/addEventListener\(\s*["']scroll["']/);
	});

	it("only animates opacity and transform, never display or visibility", () => {
		const revealBlock = source.slice(
			source.indexOf("@keyframes landing-reveal"),
			source.indexOf("@keyframes landing-reveal") + 600
		);
		expect(revealBlock).toMatch(/opacity/);
		expect(revealBlock).not.toMatch(/display:\s*none/);
		expect(revealBlock).not.toMatch(/visibility:\s*hidden/);
	});

	it("disables the reveal under prefers-reduced-motion", () => {
		expect(source).toMatch(/@media\s*\(prefers-reduced-motion:\s*reduce\)/);
	});
});
