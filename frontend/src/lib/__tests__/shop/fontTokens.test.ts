// Pins the font token setup in app.css. The Latin pixel stacks are defined once
// at :root and overridden only per language. A media query must never swap a
// font, because then a phone would show a different face than a desktop.

import { describe, it, expect } from "vitest";
import appCss from "../../../app.css?raw";

function countMatches(source: string, pattern: RegExp): number {
	return (source.match(pattern) ?? []).length;
}

describe("app.css font tokens", () => {
	it("defines --heading three times: root, ja/ko/zh and uk/ru", () => {
		expect(countMatches(appCss, /--heading:/g)).toBe(3);
	});

	it("defines --pypx three times: root, ja/ko/zh and uk/ru", () => {
		expect(countMatches(appCss, /--pypx:/g)).toBe(3);
	});

	it("starts the root heading stack with FatPixel and the root pypx stack with Pypx", () => {
		expect(appCss).toMatch(/--heading:\s*"FatPixel", system-ui/);
		expect(appCss).toMatch(/--pypx:\s*"Pypx", var\(--pixel\)/);
	});

	it("declares Pypx thick as weight 800 from its own file", () => {
		expect(appCss).toMatch(
			/font-family:\s*"Pypx";\s*src:\s*url\("\/fonts\/pypx-thick\.woff2[^"]*"\)[^;]*;\s*font-weight:\s*800;/
		);
	});

	it("keeps the only mobile root rule a font-size clamp", () => {
		const mobileRule = appCss.match(/@media \(max-width: 1024px\) \{([^}]*)\}/);
		expect(mobileRule).not.toBeNull();
		expect(mobileRule?.[1]).toMatch(/font-size:\s*clamp\(/);
		expect(mobileRule?.[1]).not.toMatch(/font-family/);
	});
});
