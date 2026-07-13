/**
 * @file censor.svelte.ts
 * @brief Client-side, display-only profanity masking for chat and usernames.
 * Not a moderation layer, messages/usernames reach the server and other
 * clients unmodified; this only changes what the *local viewer* sees,
 * replacing matched words with asterisks of the same length
 * (e.g. "fuck" -> "****").
 *
 * Word data (lib/data/profanityWords.ts, ~28 languages, LDNOOBW/CC BY 4.0)
 * is dynamically imported, see loadCensorData(), so it's only downloaded
 * once something actually needs it (first RichText render, or the register
 * form's username check), not baked into the main app bundle.
 */

export type CensorWordMap = Record<string, string[]>;

function escapeRegExp(word: string): string {
	return word.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
}

/**
 * Plain substring matching, deliberately no word-boundary check: an earlier
 * boundary-checked version protected words like "assassin" from
 * false-triggering on "ass", but that same character-class logic can't tell
 * "assassin" apart from a glued compound like "fuckyou", there's no clean
 * rule-based way to have one without the other, so this trades a few
 * innocent false positives for actually catching glued/compound abuse.
 * Substring matching also means a word repeated with no separator (e.g.
 * "wordwordword") is naturally caught occurrence-by-occurrence via the
 * regex's global flag, with no separate repetition-pattern needed.
 * Tune false positives later with an explicit allowlist if it becomes an
 * issue in practice, rather than trying to solve it with regex alone.
 */
function buildRegex(wordMap: CensorWordMap, languages: string[]): RegExp | null {
	const words = new Set<string>();
	for (const lang of languages) {
		for (const word of wordMap[lang] ?? []) words.add(word);
	}
	if (!words.size) return null;
	return new RegExp(
		[...words]
			.sort((a, b) => b.length - a.length)
			.map(escapeRegExp)
			.join("|"),
		"giu"
	);
}

function applyRegex(text: string, regex: RegExp | null): string {
	if (!regex) return text;
	return text.replace(regex, (match) => "*".repeat(match.length));
}

/**
 * Pure matcher, takes an explicit word map, no lazy-loading or caching
 * involved. Intentionally uncached (always rebuilds): this is the
 * general-purpose/test-facing entry point, called with arbitrary word maps
 * that can differ between calls, so caching here has no safe invalidation
 * signal. The actual hot path (censorText() below, typing in chat) has its
 * own dedicated single-entry cache instead.
 */
export function censorWithWordMap(
	text: string,
	wordMap: CensorWordMap,
	languages: string[]
): string {
	if (!text) return text;
	return applyRegex(text, buildRegex(wordMap, languages));
}

// --- Lazy-loaded, reactive singleton state ---------------------------------

let wordsByLang: CensorWordMap | null = null;
let customWords: string[] = [];
let customWordsVersion = 0;

let loadPromise: Promise<void> | null = null;
let defaultRegex: RegExp | null = null;
let customRegex: RegExp | null = null;
let customRegexVersion = -1;

export const censorState = $state({ ready: false, enabled: true });

/** Kicks off the (idempotent) dynamic import of the word-list data. */
export function loadCensorData(): Promise<void> {
	if (!loadPromise) {
		loadPromise = import("$data/profanityWords").then((mod) => {
			const words = mod.PROFANITY_WORDS;
			wordsByLang = words;
			defaultRegex = buildRegex(words, Object.keys(words));
			censorState.ready = true;
		});
	}
	return loadPromise;
}

/** Reserved for a future settings toggle, not yet wired to any UI. */
export function setCensorEnabled(enabled: boolean): void {
	censorState.enabled = enabled;
}

/**
 * Reserved for a future settings import (custom CSV/TXT word list), not
 * yet wired to any UI. Words are matched the same substring way as the
 * bundled lists, under a synthetic "custom" language bucket.
 */
export function addCustomWords(words: string[]): void {
	customWords = [
		...customWords,
		...words.map((w) => w.trim().toLowerCase()).filter((w) => w.length > 0)
	];
	customWordsVersion++;
}

export function clearCustomWords(): void {
	customWords = [];
	customWordsVersion++;
}

/**
 * Masks profane words in `text` for display. No-ops (returns `text`
 * unchanged) until loadCensorData() has resolved, or while disabled via
 * setCensorEnabled(false), both read reactively, so a component that
 * calls this from its template re-renders automatically once data loads.
 */
export function censorText(text: string, languages?: string[]): string {
	if (!text || !censorState.enabled || !censorState.ready || !wordsByLang) return text;

	if (languages) {
		// Explicit subset requested, not the hot path, no caching.
		return censorWithWordMap(text, wordsByLang, languages);
	}
	if (!customWords.length) {
		return applyRegex(text, defaultRegex);
	}
	if (!customRegex || customRegexVersion !== customWordsVersion) {
		customRegex = buildRegex({ ...wordsByLang, custom: customWords }, [
			...Object.keys(wordsByLang),
			"custom"
		]);
		customRegexVersion = customWordsVersion;
	}
	return applyRegex(text, customRegex);
}
