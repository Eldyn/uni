import { describe, it, expect, beforeAll, afterEach } from "vitest";
import {
	censorWithWordMap,
	censorText,
	loadCensorData,
	censorState,
	setCensorEnabled,
	addCustomWords,
	clearCustomWords
} from "$utils/censor.svelte";

describe("censorWithWordMap (pure matching logic, synthetic fixtures)", () => {
	const fixture = { en: ["cat", "shoo"] };

	it("returns clean text unchanged", () => {
		expect(censorWithWordMap("hello there, friend!", fixture, ["en"])).toBe("hello there, friend!");
	});

	it("returns an empty string unchanged", () => {
		expect(censorWithWordMap("", fixture, ["en"])).toBe("");
	});

	it("masks a listed word, preserving its length", () => {
		expect(censorWithWordMap("cat", fixture, ["en"])).toBe("***");
	});

	it("masks case-insensitively", () => {
		expect(censorWithWordMap("CAT", fixture, ["en"])).toBe("***");
	});

	it("preserves surrounding punctuation", () => {
		expect(censorWithWordMap("that cat!", fixture, ["en"])).toBe("that ***!");
	});

	it("masks a listed word wherever it appears, including glued into other letters", () => {
		// Deliberate: substring matching, no word-boundary check. A word like
		// "category" (containing "cat") gets masked too, the trade-off
		// chosen over trying to special-case boundaries, since that logic
		// can't reliably tell a glued compound apart from a real word.
		expect(censorWithWordMap("category", fixture, ["en"])).toBe("***egory");
	});

	it("masks each separated repetition individually", () => {
		expect(censorWithWordMap("shoo shoo shoo", fixture, ["en"])).toBe("**** **** ****");
	});

	it("masks a word repeated back-to-back with no separator, occurrence by occurrence", () => {
		expect(censorWithWordMap("shooshooshooshoo", fixture, ["en"])).toBe("****************");
	});

	it("masks a listed word directly abutting digits", () => {
		expect(censorWithWordMap("cat99", fixture, ["en"])).toBe("***99");
		expect(censorWithWordMap("99cat", fixture, ["en"])).toBe("99***");
	});

	it("masks two listed words glued together with no separator", () => {
		const both = { en: ["catz", "dogz"] };
		expect(censorWithWordMap("catzdogz", both, ["en"])).toBe("********");
	});

	it("works on non-Latin scripts (Cyrillic) without relying on ASCII \\b", () => {
		const ru = { ru: ["тест"] };
		expect(censorWithWordMap("тест", ru, ["ru"])).toBe("****");
		expect(censorWithWordMap("это тест.", ru, ["ru"])).toBe("это ****.");
	});

	it("works on scripts with no inter-word spacing (CJK)", () => {
		const ja = { ja: ["bad"] };
		expect(censorWithWordMap("xbadx", ja, ["ja"])).toBe("x***x");
	});

	it("only checks the requested languages", () => {
		const multi = { en: ["cat"], fr: ["chien"] };
		expect(censorWithWordMap("chien", multi, ["en"])).toBe("chien");
		expect(censorWithWordMap("chien", multi, ["fr"])).toBe("*****");
	});
});

describe("censorText (stateful wrapper, real bundled data)", () => {
	beforeAll(async () => {
		await loadCensorData();
	});

	afterEach(() => {
		setCensorEnabled(true);
		clearCustomWords();
	});

	it("loads and becomes ready", () => {
		expect(censorState.ready).toBe(true);
	});

	it("censors a single real slur", () => {
		expect(censorText("nigga")).toBe("*****");
	});

	it("censors the same slur repeated with no separators", () => {
		expect(censorText("nigganigganigganigga")).toBe("*".repeat("nigganigganigganigga".length));
	});

	it("censors the profane part of a glued username-style string, leaving the rest", () => {
		// Only "fuck" is an actual listed word here, "you"/"99" aren't
		// profanity and correctly stay untouched.
		expect(censorText("fuckyou99")).toBe("****you99");
	});

	it("can be disabled entirely", () => {
		setCensorEnabled(false);
		expect(censorText("nigga")).toBe("nigga");
	});

	it("supports adding custom words at runtime", () => {
		expect(censorText("bananaphone")).toBe("bananaphone");
		addCustomWords(["bananaphone"]);
		expect(censorText("bananaphone")).toBe("***********");
	});

	it("supports clearing custom words", () => {
		addCustomWords(["bananaphone"]);
		clearCustomWords();
		expect(censorText("bananaphone")).toBe("bananaphone");
	});
});
