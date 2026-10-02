import { describe, expect, it } from "vitest";
import { fxRenderProps, parseRichText } from "$utils/richText";

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
		// straight into a CSS `style` attribute, CSS keywords, url(), and
		// anything else are rejected rather than sanitized.
		expect(parseRichText("[c=red]oops[/c]")).toEqual([{ text: "[c=red]oops[/c]" }]);
		expect(parseRichText("[c=javascript:alert(1)]oops[/c]")).toEqual([
			{ text: "[c=javascript:alert(1)]oops[/c]" }
		]);
	});

	it("degrades an effect tag with an unrecognized value to literal text", () => {
		expect(parseRichText("[fx=bogus]oops[/fx]")).toEqual([{ text: "[fx=bogus]oops[/fx]" }]);
	});

	it("parses fx intensity and speed", () => {
		expect(parseRichText("[fx=shake:2,3]x[/fx]")).toEqual([
			{ text: "x", effect: "shake", effectIntensity: 2, effectSpeed: 3 }
		]);
	});

	it("defaults fx params when omitted", () => {
		expect(parseRichText("[fx=shake:2]x[/fx]")).toEqual([
			{ text: "x", effect: "shake", effectIntensity: 2, effectSpeed: 1 }
		]);
	});

	it("clamps fx params to 0-3", () => {
		expect(parseRichText("[fx=shake:9,-1]x[/fx]")[0]).toMatchObject({
			effectIntensity: 3,
			effectSpeed: 0
		});
	});

	it("degrades a speed without intensity to literal text", () => {
		expect(parseRichText("[fx=shake:,2]x[/fx]")).toEqual([{ text: "[fx=shake:,2]x[/fx]" }]);
	});

	it("round-trips a valid but unmatched fx tag with its params verbatim", () => {
		expect(parseRichText("[fx=shake:2,3]x")).toEqual([{ text: "[fx=shake:2,3]x" }]);
		expect(parseRichText("[fx=shake]x")).toEqual([{ text: "[fx=shake]x" }]);
	});

	it("keeps keyword tag as literal text when allowKeywords is false or omitted", () => {
		expect(parseRichText("[k=draw]Draw[/k]")).toEqual([{ text: "[k=draw]Draw[/k]" }]);
		expect(parseRichText("[k=draw]Draw[/k]", { allowKeywords: false })).toEqual([
			{ text: "[k=draw]Draw[/k]" }
		]);
	});

	it("parses keyword tag when allowKeywords is true", () => {
		const segments = parseRichText("Please [k=draw]Draw Two[/k] cards", { allowKeywords: true });
		expect(segments).toEqual([
			{ text: "Please " },
			{ text: "Draw Two", keyword: "draw" },
			{ text: " cards" }
		]);
	});

	it("combines keyword with bold and color formatting", () => {
		const segments = parseRichText("[k=wild][c=#ff0000]**Wild Card**[/c][/k]", {
			allowKeywords: true
		});
		expect(segments).toEqual([
			{ text: "Wild Card", bold: true, color: "#ff0000", keyword: "wild" }
		]);
	});

	it("degrades unmatched keyword tags to literal text", () => {
		expect(parseRichText("[k=draw]open only", { allowKeywords: true })).toEqual([
			{ text: "[k=draw]open only" }
		]);
		expect(parseRichText("stray[/k] close", { allowKeywords: true })).toEqual([
			{ text: "stray[/k] close" }
		]);
	});

	it("handles nested keyword tags and pops stack correctly", () => {
		const segments = parseRichText("[k=turn]On your [k=draw]draw[/k] step[/k]", {
			allowKeywords: true
		});
		expect(segments).toEqual([
			{ text: "On your ", keyword: "turn" },
			{ text: "draw", keyword: "draw" },
			{ text: " step", keyword: "turn" }
		]);
	});

	it("parses namespaced keyword ids", () => {
		expect(parseRichText("[k=vanilla:draw_pile]pile[/k]", { allowKeywords: true })).toEqual([
			{ text: "pile", keyword: "vanilla:draw_pile" }
		]);
	});

	it("rejects a keyword id with more than one colon", () => {
		expect(parseRichText("[k=a:b:c]x[/k]", { allowKeywords: true })).toEqual([
			{ text: "[k=a:b:c]x[/k]" }
		]);
	});
});

describe("fxRenderProps", () => {
	it("reproduces TextEffects' defaults at intensity/speed 1", () => {
		expect(fxRenderProps("shake")).toEqual({ shakeIntensity: 3, shakeSpeed: 0.4 });
		expect(fxRenderProps("undulate")).toEqual({ amplitude: 10, speed: 1.2 });
		expect(fxRenderProps("shine")).toEqual({ shineSpeed: 2.5 });
	});

	it("maps intensity and speed levels onto per-effect tuning", () => {
		expect(fxRenderProps("shake", 3, 0)).toEqual({ shakeIntensity: 8, shakeSpeed: 0.6 });
		expect(fxRenderProps("undulate", 2, 3)).toEqual({ amplitude: 16, speed: 0.55 });
		expect(fxRenderProps("shine", 1, 3)).toEqual({ shineSpeed: 1.0 });
	});
});
