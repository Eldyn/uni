import { describe, expect, it } from "vitest";
import { SHIPPED_LOCALES, uniGlossaryListing } from "../glossary/coreTextsHarness";
import { tagProblem } from "./richTagProblems";

/**
 * The rich text tags of ALL `uni` copy (log lines, reactions, glossary entries,
 * card texts) must be known and balanced in every locale.
 */

const textsFiles = import.meta.glob<Record<string, string>>(
	"../../../../../mods/uni/texts/*.json",
	{ eager: true, import: "default" }
);

const PLACEHOLDER_RE = /\{[^}]*\}/g;
const SAMPLE_VALUES: Record<string, string> = {
	kind: "uni:red_draw2",
	color: "red",
	colorName: "red",
	prevColor: "blue",
	prevColorName: "blue"
};
const knownKeywords = new Set(uniGlossaryListing().map((entry) => entry.id));

function withSampleParams(text: string): string {
	return text.replace(PLACEHOLDER_RE, (match) => SAMPLE_VALUES[match.slice(1, -1)] ?? "sample");
}

function textsOf(locale: string): Record<string, string> {
	const path = Object.keys(textsFiles).find((file) => file.endsWith(`/${locale}.json`));
	if (path === undefined) throw new Error(`missing texts ${locale}`);
	return textsFiles[path];
}

const isKnownKeyword = (id: string) => knownKeywords.has(id);

describe("tagProblem", () => {
	it.each(["[c=red]x", "x[/c]", "[c=red][fx=shake]x[/c][/fx]", "[k=uni:draw]x[/c]", "[/c][c=red]"])(
		"rejects the unbalanced copy %s",
		(text) => {
			expect(tagProblem(text, isKnownKeyword)).not.toBeNull();
		}
	);

	it("rejects a keyword that the glossary lacks", () => {
		expect(tagProblem("[k=uni:nope]x[/k]", isKnownKeyword)).not.toBeNull();
	});

	it("accepts nested balanced copy", () => {
		expect(tagProblem("[k=uni:draw][c=red]x[/c][/k]", isKnownKeyword)).toBeNull();
	});
});

describe("every tag of the uni copy opens and closes", () => {
	it("reads one file per shipped locale", () => {
		expect(Object.keys(textsFiles).length).toBe(SHIPPED_LOCALES.length);
	});

	it("covers the log, reaction, and glossary keys", () => {
		const keys = Object.keys(textsOf("en"));
		expect(keys.some((key) => key.startsWith("uni:log."))).toBe(true);
		expect(keys.some((key) => key.startsWith("uni:glossary."))).toBe(true);
	});

	for (const locale of SHIPPED_LOCALES) {
		it(`${locale}: no key has a tag problem`, () => {
			const problems: string[] = [];
			for (const [key, value] of Object.entries(textsOf(locale))) {
				const problem = tagProblem(withSampleParams(value), isKnownKeyword);
				if (problem !== null) problems.push(`${key}: ${problem}`);
			}
			expect(problems).toEqual([]);
		});
	}
});
