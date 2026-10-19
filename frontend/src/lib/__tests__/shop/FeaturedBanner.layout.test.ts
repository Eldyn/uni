// jsdom does no layout, so these tests read the component CSS as text. They
// pin the three rules that stop the name from collapsing into a one-character
// column on a narrow phone.

import { describe, it, expect } from "vitest";
import bannerSource from "../../components/shop/FeaturedBanner.svelte?raw";

function cssBlock(source: string, selector: string): string {
	const escaped = selector.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
	const match = source.match(new RegExp(`${escaped}\\s*\\{([^}]*)\\}`));
	if (!match) throw new Error(`rule not found: ${selector}`);
	return match[1];
}

describe("FeaturedBanner layout", () => {
	it("wraps its row so the buttons drop below on a narrow screen", () => {
		expect(cssBlock(bannerSource, ".featured-banner")).toMatch(/flex-wrap:\s*wrap/);
	});

	it("gives the info column a minimum width instead of zero", () => {
		const info = cssBlock(bannerSource, ".featured-info");
		expect(info).not.toMatch(/min-width:\s*0\s*;/);
		expect(info).toMatch(/min-width:\s*min\(8rem,\s*100%\)/);
		expect(info).toMatch(/flex:\s*1 1 8rem/);
	});

	it("breaks only inside a word that is too long, never down to one letter", () => {
		const name = cssBlock(bannerSource, ".featured-name");
		expect(name).not.toMatch(/overflow-wrap:\s*anywhere/);
		expect(name).toMatch(/overflow-wrap:\s*break-word/);
	});
});
