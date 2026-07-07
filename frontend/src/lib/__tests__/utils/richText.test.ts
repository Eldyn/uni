import { describe, expect, it } from "vitest";
import { parseRichText } from "$utils/richText";

describe("parseRichText", () => {
	it("returns a single unstyled segment for plain text", () => {
		expect(parseRichText("hello world")).toEqual([{ text: "hello world" }]);
	});

	it("returns an empty array for an empty string", () => {
		expect(parseRichText("")).toEqual([]);
	});

	it("parses **bold**", () => {
		const segments = parseRichText("**hi**");
		expect(segments).toEqual([{ text: "hi", bold: true }]);
	});

	it("parses *italic*", () => {
		const segments = parseRichText("*hi*");
		expect(segments).toEqual([{ text: "hi", italic: true }]);
	});

	it("parses nested bold+italic and preserves surrounding bold-only text", () => {
		const segments = parseRichText("**bold *and italic* text**");
		expect(segments).toEqual([
			{ text: "bold ", bold: true },
			{ text: "and italic", bold: true, italic: true },
			{ text: " text", bold: true }
		]);
	});

	it("degrades an unterminated bold marker to literal text instead of throwing", () => {
		expect(() => parseRichText("**bold")).not.toThrow();
		expect(parseRichText("**bold")).toEqual([{ text: "**bold" }]);
	});

	it("degrades an unterminated color tag to literal text instead of throwing", () => {
		expect(() => parseRichText("[c=#ff0000]oops")).not.toThrow();
		expect(parseRichText("[c=#ff0000]oops")).toEqual([{ text: "[c=#ff0000]oops" }]);
	});

	it("degrades a stray closing tag with no opener to literal text", () => {
		expect(parseRichText("hi[/c]there")).toEqual([{ text: "hi[/c]there" }]);
	});

	it("produces two adjacent bold segments with no empty segment between them", () => {
		const segments = parseRichText("**a****b**");
		expect(segments).toEqual([
			{ text: "a", bold: true },
			{ text: "b", bold: true }
		]);
	});

	it("parses a color tag", () => {
		const segments = parseRichText("[c=#ff0000]Draw Four[/c]");
		expect(segments).toEqual([{ text: "Draw Four", color: "#ff0000" }]);
	});

	it("parses an effect tag", () => {
		const segments = parseRichText("[fx=shake]+2![/fx]");
		expect(segments).toEqual([{ text: "+2!", effect: "shake" }]);
	});

	it("nests color and bold together", () => {
		const segments = parseRichText("[c=#ff0000]**Draw Four**[/c]");
		expect(segments).toEqual([{ text: "Draw Four", bold: true, color: "#ff0000" }]);
	});

	it("degrades a color tag with a non-hex value to literal text (style-injection guard)", () => {
		// Only hex colors are trusted since this value is interpolated
		// straight into a CSS `style` attribute — CSS keywords, url(), and
		// anything else are rejected rather than sanitized.
		expect(parseRichText("[c=red]oops[/c]")).toEqual([{ text: "[c=red]oops[/c]" }]);
		expect(parseRichText("[c=javascript:alert(1)]oops[/c]")).toEqual([
			{ text: "[c=javascript:alert(1)]oops[/c]" }
		]);
	});

	it("degrades an effect tag with an unrecognized value to literal text", () => {
		expect(parseRichText("[fx=bogus]oops[/fx]")).toEqual([{ text: "[fx=bogus]oops[/fx]" }]);
	});
});
