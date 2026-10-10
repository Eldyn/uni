import { describe, expect, it } from "vitest";
import { REACTION_RULES } from "../../chat/playLog/logBuilder";
import { SHIPPED_LOCALES } from "../glossary/coreTextsHarness";

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
	const poolKeys = REACTION_RULES.flatMap((rule) =>
		Object.values(rule.pools).flatMap((pool) => pool.map((variant) => variant.key))
	);

	it("lists no variant twice", () => {
		expect(new Set(poolKeys).size).toBe(poolKeys.length);
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
