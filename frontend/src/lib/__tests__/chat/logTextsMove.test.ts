import { describe, expect, it } from "vitest";
import { SHIPPED_LOCALES } from "../glossary/coreTextsHarness";

const presentationFiles = import.meta.glob<Record<string, unknown>>(
	"../../../../../mods/uni/presentations/*.json",
	{ eager: true, import: "default" }
);

const MOD_LOG_PREFIX = "uni:log.";
const LEGACY_LOG_KEY = /^log_/;
const COLOR_KEY_PREFIX = `${MOD_LOG_PREFIX}color_`;

const textsFiles = import.meta.glob<Record<string, string>>(
	"../../../../../mods/uni/texts/*.json",
	{ eager: true, import: "default" }
);
const catalogFiles = import.meta.glob<Record<string, string>>("../../../../messages/*.json", {
	eager: true,
	import: "default"
});

function textsOf(locale: string): Record<string, string> {
	const path = Object.keys(textsFiles).find((file) => file.endsWith(`/${locale}.json`));
	if (path === undefined) throw new Error(`missing texts ${locale}`);
	return textsFiles[path];
}

/** The variant pool of every `uni:log` case the shipped bindings declare. */
function variantPools(): string[][] {
	return Object.values(presentationFiles).flatMap((presentation) => {
		const binding = (presentation as Record<string, unknown>)["uni:binding"] as {
			"uni:cases": { "uni:present": { "uni:log"?: { "uni:variants": string[] } } }[];
		};
		return binding["uni:cases"]
			.map((logCase) => logCase["uni:present"]["uni:log"]?.["uni:variants"])
			.filter((variants): variants is string[] => variants !== undefined);
	});
}

/** Every log key a shipped binding can post, without the repeats a case that
 *  shares its line with another binding makes. */
const poolKeys = [...new Set(variantPools().flat())];

describe("the interface catalogs hold no play log key", () => {
	it("reads every catalog, pseudo included", () => {
		expect(Object.keys(catalogFiles).length).toBe(SHIPPED_LOCALES.length + 1);
	});

	for (const [path, catalog] of Object.entries(catalogFiles)) {
		it(`${path.slice(path.lastIndexOf("/") + 1)} holds no log_ key`, () => {
			expect(Object.keys(catalog).filter((key) => LEGACY_LOG_KEY.test(key))).toEqual([]);
		});
	}
});

describe("the variants of the play log are discoverable from the texts", () => {
	it("lists no variant twice within one case", () => {
		for (const variants of variantPools()) {
			expect(new Set(variants).size).toBe(variants.length);
		}
	});

	it("uses exactly the non-color log keys of the texts", () => {
		const textKeys = Object.keys(textsOf("en")).filter(
			(key) => key.startsWith(MOD_LOG_PREFIX) && !key.startsWith(COLOR_KEY_PREFIX)
		);
		expect([...poolKeys].sort()).toEqual(textKeys.sort());
	});

	for (const locale of SHIPPED_LOCALES) {
		it(`${locale}: holds a text for every variant and every palette color`, () => {
			const texts = textsOf(locale);
			const colorKeys = ["red", "yellow", "green", "blue"].map(
				(color) => `${COLOR_KEY_PREFIX}${color}`
			);
			for (const key of [...poolKeys, ...colorKeys]) {
				expect(texts[key]?.trim(), `${locale} ${key}`).toBeTruthy();
			}
		});
	}
});
