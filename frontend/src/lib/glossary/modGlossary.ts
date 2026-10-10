/**
 * @file modGlossary.ts
 * @brief The glossary entries that mods list in the `defs` packet.
 *
 * An entry holds translation keys only. The texts come from the mod text
 * layer. The listing is data from the network, so the registry checks it:
 * the namespace of an entry id must be the id of the listing mod, and a key
 * must be a text key of the same namespace. The core catalog (the `uni`
 * namespace, still in Paraglide) is read before this registry, so a mod cannot
 * replace a core entry.
 */

import type { FormatReporter } from "$lib/i18n/messageFormat";

export type GlossarySource = "card" | "explicit";

export interface ModGlossaryEntry {
	id: string;
	modId: string;
	labelKey: string;
	descriptionKey: string;
	source: GlossarySource;
}

// INFO: these patterns mirror IsValidTextKey in glossary_entries.cpp.
const MOD_ID = /^[a-z0-9_]{1,32}$/;
const TEXT_KEY = /^[a-z0-9_]{1,32}:[a-z0-9_]+(\.[a-z0-9_]+)*$/;
const MAX_KEY_LENGTH = 96;
const NAMESPACE_SEPARATOR = ":";

function isRecord(value: unknown): value is Record<string, unknown> {
	return typeof value === "object" && value !== null && !Array.isArray(value);
}

function namespaceOf(id: string): string {
	return id.slice(0, Math.max(id.indexOf(NAMESPACE_SEPARATOR), 0));
}

export class ModGlossary {
	#entries = new Map<string, ModGlossaryEntry>();
	#report: FormatReporter;
	#logged = new Set<string>();

	constructor(
		report: FormatReporter = {
			warn: (message) => console.warn(message),
			error: (message) => console.error(message)
		}
	) {
		this.#report = report;
	}

	/** Reads `mods[].glossary` of a `defs` packet. Replaces the previous listing. */
	ingestMods(mods: unknown): void {
		this.clear();
		if (!Array.isArray(mods)) return;
		for (const mod of mods) {
			if (!isRecord(mod) || typeof mod.id !== "string" || !Array.isArray(mod.glossary)) continue;
			if (!MOD_ID.test(mod.id)) continue;
			for (const raw of mod.glossary) this.#accept(mod.id, raw);
		}
	}

	#accept(modId: string, raw: unknown): void {
		if (!isRecord(raw)) return;
		const { id, label_key: labelKey, description_key: descriptionKey, source } = raw;
		if (typeof id !== "string" || typeof labelKey !== "string") return;
		if (typeof descriptionKey !== "string") return;
		if (namespaceOf(id) !== modId || id.length === modId.length + 1) {
			this.#report.error(
				`Glossary entry "${id}" is ignored: its namespace is not the listing mod "${modId}".`
			);
			return;
		}
		if (this.#entries.has(id)) {
			this.#report.error(`Glossary entry "${id}" is listed twice; the first one is kept.`);
			return;
		}
		this.#entries.set(id, {
			id,
			modId,
			labelKey,
			descriptionKey,
			source: source === "explicit" ? "explicit" : "card"
		});
	}

	clear(): void {
		this.#entries.clear();
		this.#logged.clear();
	}

	get(id: string): ModGlossaryEntry | undefined {
		return this.#entries.get(id);
	}

	/**
	 * The key to resolve for a key field, or null when the field is no key of
	 * the mod. A field that holds a text is an error: the entry stays usable
	 * and shows the raw field value.
	 */
	keyOrNull(entry: ModGlossaryEntry, field: string, key: string): string | null {
		const valid =
			key.length <= MAX_KEY_LENGTH && TEXT_KEY.test(key) && namespaceOf(key) === entry.modId;
		if (valid) return key;
		const logKey = `${entry.id}|${field}`;
		if (!this.#logged.has(logKey)) {
			this.#logged.add(logKey);
			this.#report.error(
				`Glossary entry "${entry.id}" has no text key in "${field}"; an entry holds keys, never texts. Showing the raw value.`
			);
		}
		return null;
	}
}
