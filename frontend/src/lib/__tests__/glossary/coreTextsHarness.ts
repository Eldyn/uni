/**
 * @file coreTextsHarness.ts
 * @brief Test support: puts the active locale and the shipped `uni` mod texts
 * in place, the way a `defs` packet and the asset route do in a match.
 */

import { vi } from "vitest";
import { storeI18n } from "$lib/stores/i18n.svelte";
import { storeModTexts } from "$lib/stores/modTexts.svelte";
import { storeModGlossary } from "$lib/stores/modGlossary.svelte";
import { textsUrl } from "$lib/i18n/modTexts";
import cardCatalog from "../../../../../tests/fixtures/card_catalog/uni.json";

export const SHIPPED_LOCALES = ["de", "en", "es", "it", "ja", "ko", "ru", "uk", "zh"] as const;
export type ShippedLocale = (typeof SHIPPED_LOCALES)[number];

const UNI_MOD_ID = "uni";
const encoder = new TextEncoder();

const textsFiles = import.meta.glob<string>("../../../../../mods/uni/texts/*.json", {
	eager: true,
	query: "?raw",
	import: "default"
});
const glossaryFiles = import.meta.glob<{
	"uni:glossary_entry": {
		"uni:description": { "uni:identifier": string };
		"uni:label_key": string;
		"uni:description_key": string;
	};
}>("../../../../../mods/uni/glossary/*.json", { eager: true, import: "default" });

const cardIds = (cardCatalog as { kind_id: string }[]).map((card) =>
	card.kind_id.slice(`${UNI_MOD_ID}:`.length)
);

export interface GlossaryListingEntry {
	id: string;
	label_key: string;
	description_key: string;
	source: "card" | "explicit";
}

async function sha256Hex(bytes: Uint8Array): Promise<string> {
	const digest = await crypto.subtle.digest("SHA-256", bytes as BufferSource);
	return Array.from(new Uint8Array(digest), (b) => b.toString(16).padStart(2, "0")).join("");
}

/** The glossary listing of the `uni` mod, built like BuildGlossaryEntries does. */
export function uniGlossaryListing(): GlossaryListingEntry[] {
	const byId = new Map<string, GlossaryListingEntry>();
	for (const cardId of cardIds) {
		const id = `${UNI_MOD_ID}:${cardId}`;
		byId.set(id, {
			id,
			label_key: `${id}.label`,
			description_key: `${id}.description`,
			source: "card"
		});
	}
	for (const file of Object.values(glossaryFiles)) {
		const entry = file["uni:glossary_entry"];
		const id = entry["uni:description"]["uni:identifier"];
		byId.set(id, {
			id,
			label_key: entry["uni:label_key"],
			description_key: entry["uni:description_key"],
			source: "explicit"
		});
	}
	return [...byId.values()];
}

export interface UniDefsMod {
	mod: { id: string; texts: { locale: string; hash: string; size: number }[]; glossary: unknown[] };
	bodies: Map<string, Uint8Array>;
}

/** The `uni` entry of a `defs` packet, with the bodies that the asset route serves. */
export async function buildUniDefsMod(): Promise<UniDefsMod> {
	const bodies = new Map<string, Uint8Array>();
	const texts = [];
	for (const [path, content] of Object.entries(textsFiles)) {
		const locale = path.slice(path.lastIndexOf("/") + 1, -".json".length);
		const bytes = encoder.encode(content);
		const hash = await sha256Hex(bytes);
		bodies.set(textsUrl(UNI_MOD_ID, locale, hash), bytes);
		texts.push({ locale, hash, size: bytes.length });
	}
	return { mod: { id: UNI_MOD_ID, texts, glossary: uniGlossaryListing() }, bodies };
}

export function stubTextsFetch(bodies: Map<string, Uint8Array>): void {
	vi.stubGlobal(
		"fetch",
		vi.fn(async (url: string) => {
			const body = bodies.get(url);
			return body === undefined
				? new Response("not found", { status: 404 })
				: new Response(body as BodyInit);
		})
	);
}

/** Installs the `uni` mod as a match `defs` packet does. */
export async function installUniDefs(): Promise<void> {
	const { mod, bodies } = await buildUniDefsMod();
	stubTextsFetch(bodies);
	const payload = { mods: [mod] };
	storeModTexts.ingestDefs(payload);
	storeModGlossary.ingestDefs(payload);
}

/** Resolves when the texts of the active locale and `en` have been fetched. */
export async function waitForUniTexts(): Promise<void> {
	await vi.waitFor(() => {
		if (storeModTexts.resolve(`${UNI_MOD_ID}:red_5.description`, {}) === null) {
			throw new Error("uni texts still loading");
		}
	});
}

export async function prepareCoreTexts(locale: ShippedLocale): Promise<void> {
	await storeI18n.setLocale(locale);
	await installUniDefs();
	await waitForUniTexts();
}
