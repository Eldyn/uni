import { describe, it, expect } from "vitest";
import { render } from "@testing-library/svelte";
import RichText from "$components/common/RichText.svelte";
import { storeAnimation } from "$stores/animation.svelte";
import { parseRichText } from "$utils/richText";
import { escapeLogText } from "$lib/chat/playLog/logRichText";

const logOptions = { allowLogTags: true };

describe("log colour spans", () => {
	it.each([
		["red", "var(--redCard)"],
		["yellow", "var(--yellowCard)"],
		["green", "var(--greenCard)"],
		["blue", "var(--blueCard)"]
	])("maps %s to the card palette", (name, cssColor) => {
		const segments = parseRichText(`[c=${name}]x[/c]`, logOptions);
		expect(segments).toEqual([{ text: "x", color: cssColor }]);
	});

	it("falls back to plain text for an unknown colour name", () => {
		const segments = parseRichText("[c=purple]x[/c]", logOptions);
		expect(segments.some((segment) => segment.color)).toBe(false);
		expect(segments.map((segment) => segment.text).join("")).toBe("[c=purple]x[/c]");
	});

	it("ignores colour names when log tags are off", () => {
		const segments = parseRichText("[c=red]x[/c]");
		expect(segments).toEqual([{ text: "[c=red]x[/c]" }]);
	});
});

describe("unmatched named colour", () => {
	it("demotes to its original source text", () => {
		const segments = parseRichText("[c=red]x", logOptions);
		expect(segments.map((segment) => segment.text).join("")).toBe("[c=red]x");
	});
});

describe("log shake span", () => {
	it("parses shake as an effect when log tags are on", () => {
		const segments = parseRichText("[fx=shake]x[/fx]", logOptions);
		expect(segments).toEqual([{ text: "x", effect: "shake" }]);
	});
});

describe("escapeLogText", () => {
	it("prefixes backslash, bracket and asterisk with a backslash", () => {
		expect(escapeLogText("a\\b[c*")).toBe("a\\\\b\\[c\\*");
	});

	it("leaves ordinary names untouched", () => {
		expect(escapeLogText("Alice 42")).toBe("Alice 42");
	});

	it.each(["[fx=shake]x[/fx]", "[c=red]x[/c]", "**x**", "x\\", "[fx=shake]x"])(
		"round-trips hostile name %s as literal text through the parser",
		(hostileName) => {
			const logLine = `[c=green]${escapeLogText(hostileName)}[/c] played`;
			const segments = parseRichText(logLine, logOptions);
			const nameSegments = segments.filter((segment) => segment.color === "var(--greenCard)");
			expect(nameSegments.map((segment) => segment.text).join("")).toBe(hostileName);
			expect(segments.some((segment) => segment.effect)).toBe(false);
			expect(segments.some((segment) => segment.bold)).toBe(false);
			expect(segments.filter((segment) => segment.color).length).toBe(nameSegments.length);
		}
	);
});

describe("RichText logTags prop prototype keys", () => {
	it.each(["constructor", "__proto__", "toString"])(
		"renders [c=%s] literally with no colour",
		(name) => {
			const markup = `[c=${name}]x[/c]`;
			const { container } = render(RichText, { props: { text: markup, logTags: true } });
			expect(container.textContent).toBe(markup);
			expect(container.querySelector("[style*='color']")).toBeNull();
			const segments = parseRichText(markup, logOptions);
			expect(segments.some((segment) => segment.color)).toBe(false);
		}
	);
});

describe("RichText logTags prop", () => {
	it("renders no shake or colour for a hostile name in a log line", () => {
		const hostileName = "[fx=shake][c=red]x";
		const { container } = render(RichText, {
			props: { text: `${escapeLogText(hostileName)} played`, logTags: true }
		});
		expect(container.querySelector(".shake-char")).toBeNull();
		expect(container.textContent).toContain(hostileName);
		expect(container.querySelector("[style*='redCard']")).toBeNull();
	});

	it("renders shake and colour from trusted log markup", () => {
		const { container } = render(RichText, {
			props: { text: "[c=red]a[/c] [fx=shake]b[/fx]", logTags: true }
		});
		expect(container.querySelector("[style*='--redCard']")).not.toBeNull();
		expect(container.querySelector(".shake-char")).not.toBeNull();
	});

	it("does not shake when the animation setting is off", () => {
		storeAnimation.enabled = false;
		const { container } = render(RichText, {
			props: { text: "[fx=shake]b[/fx]", logTags: true }
		});
		storeAnimation.enabled = true;
		expect(container.querySelector(".shake-char")).toBeNull();
		expect(container.textContent).toBe("b");
	});

	it("leaves named colours literal without the prop", () => {
		const { container } = render(RichText, { props: { text: "[c=red]a[/c]" } });
		expect(container.textContent).toBe("[c=red]a[/c]");
		expect(container.querySelector("[style*='redCard']")).toBeNull();
	});

	it("does not honour the escape character without the prop", () => {
		const { container } = render(RichText, { props: { text: "a\\[b" } });
		expect(container.textContent).toBe("a\\[b");
	});
});
