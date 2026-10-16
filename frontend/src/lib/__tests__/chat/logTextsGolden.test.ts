import { describe, expect, it } from "vitest";
import { resolveLogText } from "$lib/presentation/logText";
import { prepareCoreTexts } from "../glossary/coreTextsHarness";
import { SHIPPED_LOCALES, codeKeyOf, legacyKeyOf, templateOf } from "./logGoldenSupport";
import { PALETTE_COLORS } from "./richTagProblems";

/**
 * The golden files hold the play log texts as they were BEFORE they moved into
 * the `uni` mod. They never change by default. `UNI_UPDATE_GOLDEN=1` rewrites
 * them; do that only for an intended text change.
 *
 * The golden is keyed by the legacy key name. `fallbackToEnglish` is true when
 * a non-English locale had no text of its own for the key (missing, or the
 * same bytes as the English text).
 */

const GOLDEN_DIR_FROM_REPO = "tests/fixtures/golden_log_texts";

const presentationFiles = import.meta.glob<Record<string, unknown>>(
	"../../../../../mods/uni/presentations/*.json",
	{ eager: true, import: "default" }
);

const SAMPLE_PARAMS = {
	name: "Ann",
	kind: "uni:red_draw2",
	color: "red",
	prevColor: "blue",
	count: 2,
	amount: 2,
	total: 4,
	handSize: 5,
	drawRun: 3,
	victim: "Bob",
	blocker: "Bob",
	target: "Ann",
	place: 1
};

interface GoldenEntry {
	template: string;
	resolved: string;
	fallbackToEnglish: boolean;
}
type GoldenLocale = Record<string, GoldenEntry>;

/** The parameters of one log case: a sample for every name it declares, so
 *  the copy renders the way the case renders it in the game. */
function caseParams(params: object): Record<string, string | number> {
	return Object.fromEntries(
		Object.keys(params).map((name) => [name, SAMPLE_PARAMS[name as keyof typeof SAMPLE_PARAMS] ?? "Bob"])
	);
}

/** The log keys the shipped bindings can post, with the params of the case
 *  that posts them. A key two bindings share keeps the first case's params. */
function variantKeys(): { key: string; params: Record<string, string | number> }[] {
	const variants: { key: string; params: Record<string, string | number> }[] = [];
	for (const presentation of Object.values(presentationFiles)) {
		const binding = (presentation as Record<string, unknown>)["uni:binding"] as {
			"uni:cases": {
				"uni:present": { "uni:log"?: { "uni:variants"?: string[]; "uni:params"?: object } };
			}[];
		};
		for (const logCase of binding["uni:cases"]) {
			const log = logCase["uni:present"]["uni:log"];
			if (log === undefined || log["uni:variants"] === undefined) continue;
			for (const key of log["uni:variants"]) {
				variants.push({ key, params: caseParams(log["uni:params"] ?? {}) });
			}
		}
	}
	return variants;
}

const VARIANTS = variantKeys();
const PARAMS_OF_KEY = new Map(VARIANTS.map((variant) => [variant.key, variant.params]));
/** The colour words the log names; no binding declares them as a variant. */
const COLOR_KEYS = PALETTE_COLORS.map((color) => `uni:log.color_${color}`);

const LEGACY_KEYS = [
	...new Set(VARIANTS.map((variant) => legacyKeyOf(variant.key))),
	...COLOR_KEYS.map((key) => legacyKeyOf(key))
];

function renderLocale(locale: string): GoldenLocale {
	const entries: GoldenLocale = {};
	for (const legacyKey of LEGACY_KEYS) {
		const template = templateOf(locale, legacyKey);
		const english = templateOf("en", legacyKey);
		entries[legacyKey] = {
			template: template ?? "",
			resolved:
				resolveLogText(
					codeKeyOf(legacyKey),
					PARAMS_OF_KEY.get(codeKeyOf(legacyKey)) ?? SAMPLE_PARAMS,
					{ locale }
				) ?? "",
			fallbackToEnglish: locale !== "en" && (template === undefined || template === english)
		};
	}
	return entries;
}

const goldenFiles = import.meta.glob<unknown>(
	"../../../../../tests/fixtures/golden_log_texts/*.json",
	{
		eager: true,
		import: "default"
	}
);

function storedFor<T>(name: string): T | undefined {
	const key = Object.keys(goldenFiles).find((path) => path.endsWith(`/${name}.json`));
	return key === undefined ? undefined : (goldenFiles[key] as T);
}

const nodeModule = (name: string) => import(/* @vite-ignore */ `node:${name}`);
const updateRequested = (globalThis as { process?: { env?: Record<string, string> } }).process?.env
	?.UNI_UPDATE_GOLDEN;

async function writeGolden(name: string, content: unknown): Promise<void> {
	const { writeFileSync } = await nodeModule("fs");
	const { dirname, resolve } = await nodeModule("path");
	const { fileURLToPath } = await nodeModule("url");
	const repoRoot = resolve(dirname(fileURLToPath(import.meta.url)), "../../../../..");
	writeFileSync(
		resolve(repoRoot, GOLDEN_DIR_FROM_REPO, `${name}.json`),
		JSON.stringify(content, null, 2) + "\n"
	);
}

describe("play log texts equal the stored golden in every locale", () => {
	it("lists each log key once", () => {
		expect(new Set(LEGACY_KEYS).size).toBe(LEGACY_KEYS.length);
	});

	for (const locale of SHIPPED_LOCALES) {
		it(`locale ${locale}`, async () => {
			await prepareCoreTexts(locale);
			const rendered = renderLocale(locale);
			if (updateRequested) {
				await writeGolden(locale, rendered);
				return;
			}
			const stored = storedFor<GoldenLocale>(locale);
			expect(stored, `missing golden file for ${locale}`).toBeDefined();
			expect(Object.keys(rendered).sort()).toEqual(Object.keys(stored!).sort());
			for (const key of Object.keys(stored!)) {
				const expected = stored![key];
				if (expected.fallbackToEnglish) continue;
				expect(rendered[key], `${locale} ${key}`).toEqual(expected);
			}
		});
	}
});
