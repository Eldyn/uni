import { describe, expect, it } from "vitest";
import { getGlossaryEntry, type GlossaryEntry } from "$lib/glossary/glossary";
import { cardInfoByKind, getCardInfo } from "$lib/glossary/cardDescriptions";
import type { CardValue } from "$stores/game.svelte";
import { SHIPPED_LOCALES, prepareCoreTexts } from "./coreTextsHarness";

/**
 * The golden files hold what the glossary and the card descriptions showed
 * BEFORE the texts moved into the `uni` mod. They never change by default.
 * `UNI_UPDATE_GOLDEN=1` rewrites them; do that only for an intended text change.
 */

const GOLDEN_DIR_FROM_REPO = "tests/fixtures/golden_glossary_core";

const CORE_KEYWORDS = [
	"draw",
	"skip",
	"reverse",
	"wild",
	"turn",
	"color",
	"draw_pile",
	"discard_pile",
	"play",
	"seven_zero",
	"draw_stacking",
	"force_play",
	"jump_in",
	"progressive",
	"race"
].map((name) => `uni:${name}`);

interface CardFile {
	id: string;
	face: { color: string; label: string };
}

interface GoldenLocale {
	entries: Record<string, GlossaryEntry | null>;
	cardKinds: Record<string, { title: string; description: string } | null>;
	cardInfos: Record<string, { title: string; description: string } | null>;
}

const cardFiles = import.meta.glob<CardFile>("../../../../../mods/uni/cards/*.json", {
	eager: true,
	import: "default"
});
const goldenFiles = import.meta.glob<GoldenLocale>(
	"../../../../../tests/fixtures/golden_glossary_core/*.json",
	{ eager: true, import: "default" }
);

const cards = Object.values(cardFiles).sort((a, b) => a.id.localeCompare(b.id));

function render(locale: string): GoldenLocale {
	const entries: GoldenLocale["entries"] = {};
	for (const id of [...CORE_KEYWORDS, ...cards.map((card) => `uni:${card.id}`)]) {
		entries[id] = getGlossaryEntry(id, locale);
	}
	const cardKinds: GoldenLocale["cardKinds"] = {};
	const cardInfos: GoldenLocale["cardInfos"] = {};
	for (const card of cards) {
		cardKinds[`uni:${card.id}`] = cardInfoByKind(`uni:${card.id}`);
		cardInfos[card.id] = getCardInfo({
			type: card.face.color,
			value: card.face.label as CardValue
		});
	}
	return { entries, cardKinds, cardInfos };
}

function storedFor(locale: string): GoldenLocale | undefined {
	const key = Object.keys(goldenFiles).find((path) => path.endsWith(`/${locale}.json`));
	return key === undefined ? undefined : goldenFiles[key];
}

const nodeModule = (name: string) => import(/* @vite-ignore */ `node:${name}`);
const updateRequested = (globalThis as { process?: { env?: Record<string, string> } }).process?.env
	?.UNI_UPDATE_GOLDEN;

async function writeGolden(locale: string, content: GoldenLocale): Promise<void> {
	const { writeFileSync } = await nodeModule("fs");
	const { dirname, resolve } = await nodeModule("path");
	const { fileURLToPath } = await nodeModule("url");
	const repoRoot = resolve(dirname(fileURLToPath(import.meta.url)), "../../../../..");
	writeFileSync(
		resolve(repoRoot, GOLDEN_DIR_FROM_REPO, `${locale}.json`),
		JSON.stringify(content, null, 2) + "\n"
	);
}

describe("core glossary and card texts equal the stored golden in every locale", () => {
	it("covers the expected ids", () => {
		expect(cards.length).toBe(54);
		expect(Object.keys(goldenFiles).length).toBe(SHIPPED_LOCALES.length);
	});

	for (const locale of SHIPPED_LOCALES) {
		it(`locale ${locale}`, async () => {
			await prepareCoreTexts(locale);
			const rendered = render(locale);
			if (updateRequested) {
				await writeGolden(locale, rendered);
				return;
			}
			const stored = storedFor(locale);
			expect(stored, `missing golden file for ${locale}`).toBeDefined();
			expect(rendered).toEqual(stored);
		});
	}
});

describe("the golden itself is complete", () => {
	for (const locale of SHIPPED_LOCALES) {
		it(`locale ${locale} has no empty entry`, () => {
			const stored = storedFor(locale);
			if (updateRequested || stored === undefined) return;
			for (const [id, entry] of Object.entries(stored.entries)) {
				expect(entry, id).not.toBeNull();
				expect(entry!.title.length, id).toBeGreaterThan(0);
				expect(entry!.description.length, id).toBeGreaterThan(0);
			}
			for (const [id, info] of Object.entries(stored.cardInfos)) {
				expect(info, id).not.toBeNull();
				expect(info!.description.length, id).toBeGreaterThan(0);
			}
		});
	}
});
