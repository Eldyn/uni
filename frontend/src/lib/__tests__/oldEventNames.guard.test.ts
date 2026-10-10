import { describe, expect, it } from "vitest";

/**
 * No client file names an old event or an old envelope field. The old names are
 * built from parts so that this file stays clean. A line about a hook or about
 * analytics is allowed: a hook keeps its bare name and is not an event.
 */

const sourceFiles = import.meta.glob(["../../**/*.ts", "../../**/*.svelte"], {
	eager: true,
	query: "?raw",
	import: "default"
}) as Record<string, string>;
const dataFiles = import.meta.glob(
	["../../../../contract/asyncapi.yaml", "../../../../mods/**/*.json"],
	{ eager: true, query: "?raw", import: "default" }
) as Record<string, string>;

const skippedFiles = ["oldEventNames.guard.test.ts", "analytics.svelte.ts"];

const oldEventNames = [
	"window" + "_open",
	"window" + "_response",
	"window" + "_close",
	"prompt" + "_open",
	"prompt" + "_close",
	"roll" + "_result",
	"outcome" + "_result",
	"place" + "ment",
	"match" + "_end"
];
const alternation = oldEventNames.join("|");
const oldSequenceName = "s" + "eq";

const quotedName = new RegExp(`(["'\`])(${alternation})\\1`);
const yamlKey = new RegExp(`^\\s*(${alternation}):`);
const quotedField = new RegExp(`"${oldSequenceName}"`);
const memberField = new RegExp(`\\b(env|envelope|entry|packet|frame|data)\\.${oldSequenceName}\\b`);
const yamlField = new RegExp(`^\\s*${oldSequenceName}:|required: \\[${oldSequenceName}[,\\]]`);

function isHookLine(line: string): boolean {
	return /hook|Hook|track\(|nalytics|callsFor\(/.test(line);
}

function findOldNames(): string[] {
	const findings: string[] = [];
	const all = { ...sourceFiles, ...dataFiles };
	for (const [path, text] of Object.entries(all)) {
		if (skippedFiles.some((name) => path.endsWith(`/${name}`))) continue;
		const isYaml = path.endsWith(".yaml");
		const isTypeScript = path.endsWith(".ts");
		text.split("\n").forEach((line: string, index: number) => {
			const named = !isHookLine(line) && (quotedName.test(line) || (isYaml && yamlKey.test(line)));
			const field =
				quotedField.test(line) ||
				(isYaml && yamlField.test(line)) ||
				(isTypeScript && memberField.test(line));
			if (named || field) findings.push(`${path}:${index + 1}: ${line.trim()}`);
		});
	}
	return findings;
}

describe("old event names", () => {
	it("scans the client, the contract and the mods", () => {
		expect(Object.keys(sourceFiles).length).toBeGreaterThan(100);
		expect(Object.keys(dataFiles).some((path) => path.endsWith("asyncapi.yaml"))).toBe(true);
	});

	it("no old event name or old envelope field remains", () => {
		expect(findOldNames().slice(0, 40)).toEqual([]);
	});

	it("the pattern finds a planted old name and skips a new name", () => {
		expect(quotedName.test(`emit("${oldEventNames[0]}")`)).toBe(true);
		expect(quotedName.test('emit("window_opened")')).toBe(false);
		expect(quotedName.test('emit("placement_set")')).toBe(false);
		expect(memberField.test(`env.data.${oldSequenceName}`)).toBe(true);
		expect(memberField.test(`beat.${oldSequenceName}`)).toBe(false);
	});
});
