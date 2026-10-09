import { describe, it, expect, vi } from "vitest";
import { formatMessage, type FormatReporter } from "$lib/i18n/messageFormat";
import { parseRichText } from "$utils/richText";
import { escapeLogText } from "$lib/chat/playLog/logRichText";

function reporter() {
	return { warn: vi.fn(), error: vi.fn() } satisfies FormatReporter;
}

const CARD_PLURAL = "{count, plural, one {# card} other {# cards}}";

describe("formatMessage placeholders", () => {
	it("returns plain text unchanged", () => {
		expect(formatMessage("Hello there", {}, { locale: "en" })).toBe("Hello there");
	});

	it("fills string and number placeholders", () => {
		expect(formatMessage("{name} drew {count}.", { name: "Ana", count: 3 }, { locale: "en" })).toBe(
			"Ana drew 3."
		);
	});

	it("fills the same placeholder twice", () => {
		expect(formatMessage("{a}-{a}", { a: "x" }, { locale: "en" })).toBe("x-x");
	});

	it("leaves a missing placeholder in the text and warns once without throwing", () => {
		const report = reporter();
		const text = formatMessage("{name} drew {count}.", { count: 2 }, { locale: "en", report });
		expect(text).toBe("{name} drew 2.");
		expect(report.warn).toHaveBeenCalledTimes(1);
		expect(report.warn.mock.calls[0]![0]).toContain("name");
		expect(report.error).not.toHaveBeenCalled();
	});

	it("does not expand a placeholder inside a value", () => {
		expect(formatMessage("{name}", { name: "{count}", count: 9 }, { locale: "en" })).toBe(
			"{count}"
		);
	});
});

describe("formatMessage escapes", () => {
	it("turns an escaped brace into a literal brace", () => {
		expect(formatMessage("\\{name\\} is {name}", { name: "Ana" }, { locale: "en" })).toBe(
			"{name} is Ana"
		);
	});

	it("turns an escaped hash into a literal hash inside a plural", () => {
		const pattern = "{count, plural, one {\\# is # card} other {\\# is # cards}}";
		expect(formatMessage(pattern, { count: 2 }, { locale: "en" })).toBe("# is 2 cards");
	});

	it("keeps the rich text escapes for the rich text parser", () => {
		expect(formatMessage("\\[x\\] \\* \\\\", {}, { locale: "en" })).toBe("\\[x\\] \\* \\\\");
	});

	it("keeps a pair of backslashes before a brace as a pair", () => {
		expect(formatMessage("\\\\{name}", { name: "Ana" }, { locale: "en" })).toBe("\\\\Ana");
	});
});

describe("formatMessage rich text tags", () => {
	it("passes the colour, effect, and keyword tags through", () => {
		const text = formatMessage(
			"[c=red]{name}[/c] plays [fx=shake:2]{card}[/fx] [k=uni:skip]skip[/k]",
			{ name: "Ana", card: "Skip" },
			{ locale: "en" }
		);
		expect(text).toBe("[c=red]Ana[/c] plays [fx=shake:2]Skip[/fx] [k=uni:skip]skip[/k]");
	});

	it("gives the same segments as the hand written string", () => {
		const options = { allowLogTags: true, allowKeywords: true };
		const formatted = formatMessage(
			"[c=red]{name}[/c] has [k=uni:skip]{count, plural, one {# skip} other {# skips}}[/k]",
			{ name: "Ana", count: 2 },
			{ locale: "en" }
		);
		const written = "[c=red]Ana[/c] has [k=uni:skip]2 skips[/k]";
		expect(parseRichText(formatted, options)).toEqual(parseRichText(written, options));
	});
});

describe("formatMessage plurals", () => {
	it("uses one and other in English", () => {
		const english = (count: number) => formatMessage(CARD_PLURAL, { count }, { locale: "en" });
		expect(english(1)).toBe("1 card");
		expect(english(0)).toBe("0 cards");
		expect(english(2)).toBe("2 cards");
	});

	it("uses the three forms of Russian", () => {
		const pattern = "{count, plural, one {# карта} few {# карты} many {# карт} other {# карты}}";
		const russian = (count: number) => formatMessage(pattern, { count }, { locale: "ru" });
		expect(russian(1)).toBe("1 карта");
		expect(russian(21)).toBe("21 карта");
		expect(russian(2)).toBe("2 карты");
		expect(russian(4)).toBe("4 карты");
		expect(russian(5)).toBe("5 карт");
		expect(russian(11)).toBe("11 карт");
		expect(russian(0)).toBe("0 карт");
		expect(russian(1.5)).toBe("1.5 карты");
	});

	it("uses the forms of Polish", () => {
		const pattern = "{count, plural, one {# karta} few {# karty} many {# kart} other {# karty}}";
		const polish = (count: number) => formatMessage(pattern, { count }, { locale: "pl" });
		expect(polish(1)).toBe("1 karta");
		expect(polish(3)).toBe("3 karty");
		expect(polish(5)).toBe("5 kart");
		expect(polish(22)).toBe("22 karty");
	});

	it("uses the six forms of Arabic", () => {
		const pattern =
			"{count, plural, zero {z #} one {o #} two {t #} few {f #} many {m #} other {x #}}";
		const arabic = (count: number) => formatMessage(pattern, { count }, { locale: "ar" });
		expect(arabic(0)).toBe("z 0");
		expect(arabic(1)).toBe("o 1");
		expect(arabic(2)).toBe("t 2");
		expect(arabic(3)).toBe("f 3");
		expect(arabic(11)).toBe("m 11");
		expect(arabic(100)).toBe("x 100");
	});

	it("uses the one form of Japanese", () => {
		const pattern = "{count, plural, other {#枚}}";
		const japanese = (count: number) => formatMessage(pattern, { count }, { locale: "ja" });
		expect(japanese(1)).toBe("1枚");
		expect(japanese(5)).toBe("5枚");
	});

	it("falls back to other when the category has no branch", () => {
		const pattern = "{count, plural, one {# card} other {# cards}}";
		expect(formatMessage(pattern, { count: 3 }, { locale: "ru" })).toBe("3 cards");
	});

	it("fills a placeholder inside a branch", () => {
		const pattern = "{count, plural, one {{name} has # card} other {{name} has # cards}}";
		expect(formatMessage(pattern, { name: "Ana", count: 1 }, { locale: "en" })).toBe(
			"Ana has 1 card"
		);
		expect(formatMessage(pattern, { name: "Ana", count: 4 }, { locale: "en" })).toBe(
			"Ana has 4 cards"
		);
	});

	it("reads # from the closest plural", () => {
		const pattern = "{a, plural, other {# and {b, plural, one {# one} other {# many}}}}";
		expect(formatMessage(pattern, { a: 7, b: 1 }, { locale: "en" })).toBe("7 and 1 one");
	});

	it("reads a numeric string as a number", () => {
		expect(formatMessage(CARD_PLURAL, { count: "1" }, { locale: "en" })).toBe("1 card");
	});

	it("keeps a hash outside a plural as text", () => {
		expect(formatMessage("Card #1", {}, { locale: "en" })).toBe("Card #1");
	});

	it("keeps the plural text and warns when the count is missing or no number", () => {
		const report = reporter();
		expect(formatMessage(CARD_PLURAL, {}, { locale: "en", report })).toBe(CARD_PLURAL);
		expect(formatMessage(CARD_PLURAL, { count: "many" }, { locale: "en", report })).toBe(
			CARD_PLURAL
		);
		expect(report.warn).toHaveBeenCalledTimes(2);
	});
});

describe("formatMessage malformed patterns", () => {
	it.each([
		["an open brace", "Hello {name"],
		["a stray close brace", "Hello }"],
		["an empty placeholder", "Hello {}"],
		["a bad name", "Hello {1abc}"],
		["no other branch", "{count, plural, one {# card}}"],
		["an unknown format", "{count, select, a {x} other {y}}"],
		["an exact match branch", "{count, plural, =0 {none} other {some}}"],
		["an unknown category", "{count, plural, lots {x} other {y}}"],
		["an open branch", "{count, plural, one {# card other {# cards}}"],
		["a duplicate branch", "{count, plural, one {a} one {b} other {c}}"]
	])("returns the raw string and logs an error for %s", (_label, pattern) => {
		const report = reporter();
		const text = formatMessage(pattern, { count: 1, name: "Ana" }, { locale: "en", report });
		expect(text).toBe(pattern);
		expect(report.error).toHaveBeenCalledTimes(1);
	});

	it("never throws on random input", () => {
		const report = reporter();
		const alphabet = ["{", "}", ",", " ", "#", "\\", "plural", "one", "other", "a", "[", "]"];
		let seed = 7;
		const next = () => {
			seed = (seed * 1103515245 + 12345) & 0x7fffffff;
			return seed;
		};
		for (let run = 0; run < 300; run += 1) {
			let pattern = "";
			for (let i = 0; i < 12; i += 1) pattern += alphabet[next() % alphabet.length];
			expect(() => formatMessage(pattern, { a: 1 }, { locale: "en", report })).not.toThrow();
		}
	});
});

describe("formatMessage parameter escaping contract", () => {
	it("keeps a raw parameter value as written: callers must escape it", () => {
		const raw = "[fx=boom]x[/fx]";
		const text = formatMessage("{name} wins", { name: raw }, { locale: "en" });
		expect(text).toBe(`${raw} wins`);
		expect(parseRichText(text).some((part) => JSON.stringify(part).includes("boom"))).toBe(true);
	});

	it("keeps a parameter literal once the caller escaped it", () => {
		const escaped = escapeLogText("[fx=boom]x[/fx]");
		const text = formatMessage("{name} wins", { name: escaped }, { locale: "en" });
		expect(text).toBe(`${escaped} wins`);
		expect(text).not.toBe("[fx=boom]x[/fx] wins");
	});
});
