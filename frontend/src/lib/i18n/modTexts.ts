/**
 * @file modTexts.ts
 * @brief The run-time layer of mod texts: locale, then key, then string.
 *
 * The `defs` packet lists each texts file of a mod with its SHA-256. The layer
 * fetches only the active locale and `en`. It checks the hash of the bytes
 * BEFORE it parses them. A file that fails the check is dropped with an error
 * log, and the key falls back. Paraglide is the only source of the active
 * locale: the layer reads it through `getLocale` and never stores it.
 *
 * Fallback for one key: the active locale, then `en` (a warning), then the raw
 * key (an error for the mod developer). The layer never throws. Each log
 * appears once for each key and locale.
 */

import { verifyAssetBytes } from "$components/game/three/assetTrust";
import { formatMessage, type FormatParams, type FormatReporter } from "./messageFormat";
import { pseudoLocalizeText, wrapPseudo } from "./pseudoLocale";

export const PSEUDO_LOCALE = "pseudo";
export const FALLBACK_LOCALE = "en";

// INFO: these limits mirror include/match/modload/texts_limits.hpp.
const MAX_TEXTS_FILE_BYTES = 256 * 1024;
const MAX_TEXT_VALUE_BYTES = 2 * 1024;
const MAX_TEXT_KEYS = 5000;

// INFO: these two patterns mirror IsValidLocaleTag in texts_limits.cpp and the
// mod id rule of the loader. They run before any URL is built.
const LOCALE_TAG = /^[a-z]{2,3}(-[A-Za-z0-9]{2,8})*$/;
const MOD_ID = /^[a-z0-9_]+$/;
const MAX_LOGGED_KEYS = 2000;

const FULL_HASH = /^[0-9a-f]{64}$/;
const URL_HASH_LENGTH = 16;
const NAMESPACE_SEPARATOR = ":";

export interface TextsListingEntry {
	locale: string;
	/** SHA-256 of the bytes of the file, 64 lowercase hex characters. */
	hash: string;
	size: number;
}

export interface ModTextsDeps {
	/** The active locale. Production code passes Paraglide's `getLocale`. */
	getLocale: () => string;
	fetchBytes: (url: string) => Promise<ArrayBuffer>;
	/** Called when a file finishes loading, so readers render again. */
	onChange: () => void;
	report?: FormatReporter;
}

type FileState =
	| { status: "pending" }
	| { status: "loaded"; strings: ReadonlyMap<string, string> }
	| { status: "failed" };

const textEncoder = new TextEncoder();

/**
 * The URL of a texts slot: ids and the short hash only, like every asset. The
 * URL is always a path on the page origin. Each id is encoded, so an id cannot
 * add a segment or a host.
 */
export function textsUrl(modId: string, locale: string, hash: string): string {
	return `/assets/${encodeURIComponent(modId)}/texts/${encodeURIComponent(locale)}/low/${hash.slice(0, URL_HASH_LENGTH)}`;
}

/** The default fetch: same origin, no credentials. */
export async function fetchTextsBytes(url: string): Promise<ArrayBuffer> {
	const response = await fetch(url, { credentials: "omit" });
	if (!response.ok) throw new Error(`HTTP ${response.status}`);
	return response.arrayBuffer();
}

function isRecord(value: unknown): value is Record<string, unknown> {
	return typeof value === "object" && value !== null && !Array.isArray(value);
}

export class ModTexts {
	#deps: ModTextsDeps;
	#listing = new Map<string, Map<string, TextsListingEntry>>();
	#files = new Map<string, FileState>();
	#contentByHash = new Map<string, ReadonlyMap<string, string>>();
	#logged = new Set<string>();
	#generation = 0;

	constructor(deps: ModTextsDeps) {
		this.#deps = deps;
	}

	get #report(): FormatReporter {
		return this.#deps.report ?? { warn: console.warn, error: console.error };
	}

	/** Reads `mods[].texts` of a `defs` packet. Replaces the previous listing. */
	ingestMods(mods: unknown): void {
		this.clear();
		if (!Array.isArray(mods)) return;
		for (const mod of mods) {
			if (!isRecord(mod) || typeof mod.id !== "string" || !Array.isArray(mod.texts)) continue;
			if (!MOD_ID.test(mod.id)) {
				this.#report.error(`Texts of mod "${mod.id}" are ignored: the mod id is not valid.`);
				continue;
			}
			const byLocale = new Map<string, TextsListingEntry>();
			for (const raw of mod.texts) {
				const entry = this.#acceptListing(mod.id, raw);
				if (entry !== null) byLocale.set(entry.locale, entry);
			}
			if (byLocale.size > 0) this.#listing.set(mod.id, byLocale);
		}
	}

	#acceptListing(modId: string, raw: unknown): TextsListingEntry | null {
		if (!isRecord(raw) || typeof raw.locale !== "string") return null;
		const { locale, hash, size } = raw;
		if (locale === PSEUDO_LOCALE || locale.length > 32 || !LOCALE_TAG.test(locale)) {
			this.#report.error(`A texts file of "${modId}" is ignored: its locale tag is not valid.`);
			return null;
		}
		if (typeof hash !== "string" || !FULL_HASH.test(hash)) {
			this.#report.error(
				`Texts file "${modId}/${locale}" is ignored: its hash is not a full SHA-256.`
			);
			return null;
		}
		if (typeof size !== "number" || !(size >= 0) || size > MAX_TEXTS_FILE_BYTES) {
			this.#report.error(
				`Texts file "${modId}/${locale}" is ignored: its size is over ${MAX_TEXTS_FILE_BYTES} bytes.`
			);
			return null;
		}
		return { locale, hash, size };
	}

	/** Forgets the listing of the match. The content stays cached by hash. */
	clear(): void {
		this.#generation += 1;
		this.#listing.clear();
		this.#files.clear();
		this.#logged.clear();
	}

	/** Drops the verified content too, so the next sync fetches again. */
	forgetContent(): void {
		this.#contentByHash.clear();
		this.#logged.clear();
	}

	/** Fetches the files for the active locale and `en`. Resolves when all end. */
	sync(): Promise<void> {
		const active = this.#deps.getLocale();
		const wanted = new Set([FALLBACK_LOCALE]);
		if (active !== PSEUDO_LOCALE) wanted.add(active);
		const loads: Promise<void>[] = [];
		let loadedFromCache = false;
		for (const [modId, byLocale] of this.#listing) {
			for (const locale of wanted) {
				const entry = byLocale.get(locale);
				const fileId = `${modId}/${locale}`;
				if (entry === undefined || this.#files.has(fileId)) continue;
				const cached = this.#contentByHash.get(`${fileId}/${entry.hash}`);
				if (cached !== undefined) {
					this.#files.set(fileId, { status: "loaded", strings: cached });
					loadedFromCache = true;
					continue;
				}
				this.#files.set(fileId, { status: "pending" });
				loads.push(this.#load(modId, entry));
			}
		}
		if (loadedFromCache) this.#deps.onChange();
		return Promise.all(loads).then(() => undefined);
	}

	async #load(modId: string, entry: TextsListingEntry): Promise<void> {
		const generation = this.#generation;
		const fileId = `${modId}/${entry.locale}`;
		let strings: ReadonlyMap<string, string> | null = null;
		try {
			const url = textsUrl(modId, entry.locale, entry.hash);
			const bytes = await this.#deps.fetchBytes(url);
			if (await verifyAssetBytes(bytes, entry.hash)) {
				strings = this.#parseVerified(modId, entry.locale, bytes);
			} else {
				this.#report.error(
					`Texts file "${fileId}" is rejected: the bytes do not match the hash of the defs.`
				);
			}
		} catch (error) {
			this.#report.error(`Texts file "${fileId}" failed to load: ${String(error)}.`);
		}
		if (strings !== null) this.#contentByHash.set(`${fileId}/${entry.hash}`, strings);
		if (generation !== this.#generation) return;
		this.#files.set(
			fileId,
			strings === null ? { status: "failed" } : { status: "loaded", strings }
		);
		this.#deps.onChange();
	}

	/** Parses bytes whose hash is already checked. */
	#parseVerified(
		modId: string,
		locale: string,
		bytes: ArrayBuffer
	): ReadonlyMap<string, string> | null {
		const fileId = `${modId}/${locale}`;
		const parsed: unknown = JSON.parse(new TextDecoder("utf-8", { fatal: true }).decode(bytes));
		if (!isRecord(parsed)) {
			this.#report.error(`Texts file "${fileId}" is rejected: it is no flat object.`);
			return null;
		}
		const prefix = modId + NAMESPACE_SEPARATOR;
		const strings = new Map<string, string>();
		let dropped = 0;
		for (const [key, value] of Object.entries(parsed)) {
			const accepted =
				typeof value === "string" &&
				key.length > prefix.length &&
				key.startsWith(prefix) &&
				textEncoder.encode(value).length <= MAX_TEXT_VALUE_BYTES &&
				strings.size < MAX_TEXT_KEYS;
			if (accepted) strings.set(key, value);
			else dropped += 1;
		}
		if (dropped > 0) {
			this.#report.warn(
				`Texts file "${fileId}": ${dropped} entries are dropped (not a string, outside the namespace, or over a limit).`
			);
		}
		return strings;
	}

	#isPending(modId: string, locale: string): boolean {
		const entry = this.#listing.get(modId)?.get(locale);
		if (entry === undefined) return false;
		const state = this.#files.get(`${modId}/${locale}`);
		return state === undefined || state.status === "pending";
	}

	#lookup(modId: string, locale: string, key: string): string | undefined {
		const state = this.#files.get(`${modId}/${locale}`);
		return state?.status === "loaded" ? state.strings.get(key) : undefined;
	}

	#once(level: "warn" | "error", id: string, message: string): void {
		const logKey = `${level}|${id}`;
		if (this.#logged.has(logKey) || this.#logged.size >= MAX_LOGGED_KEYS) return;
		this.#logged.add(logKey);
		this.#report[level](message);
	}

	/**
	 * The text of a key, formatted. Null while a needed file loads, or when the
	 * key has no mod namespace (the interface catalog owns such keys).
	 */
	resolve(key: string, params: FormatParams): string | null {
		const separator = key.indexOf(NAMESPACE_SEPARATOR);
		if (separator <= 0) return null;
		const modId = key.slice(0, separator);
		const active = this.#deps.getLocale();
		const pseudo = active === PSEUDO_LOCALE;
		if ([active, FALLBACK_LOCALE].some((locale) => this.#isPending(modId, locale))) return null;

		if (!pseudo) {
			const own = this.#lookup(modId, active, key);
			if (own !== undefined) return this.#format(own, params, active, key, false);
		}
		const english = this.#lookup(modId, FALLBACK_LOCALE, key);
		if (english !== undefined) {
			if (!pseudo && active !== FALLBACK_LOCALE) {
				this.#once(
					"warn",
					`${key}|${active}`,
					`Text key "${key}" is missing in locale "${active}"; using "${FALLBACK_LOCALE}".`
				);
			}
			return this.#format(english, params, FALLBACK_LOCALE, key, pseudo);
		}
		this.#once(
			"error",
			`${key}|${active}`,
			`Text key "${key}" is missing in "${FALLBACK_LOCALE}" (locale "${active}"); showing the raw key.`
		);
		return key;
	}

	#format(
		pattern: string,
		params: FormatParams,
		locale: string,
		key: string,
		pseudo: boolean
	): string {
		const report: FormatReporter = {
			warn: (message) =>
				this.#once("warn", `${key}|${locale}|${message}`, `${message} (key "${key}")`),
			error: (message) =>
				this.#once("error", `${key}|${locale}|${message}`, `${message} (key "${key}")`)
		};
		const formatted = formatMessage(pattern, params, {
			locale,
			report,
			transformText: pseudo ? pseudoLocalizeText : undefined
		});
		return pseudo ? wrapPseudo(formatted) : formatted;
	}
}
