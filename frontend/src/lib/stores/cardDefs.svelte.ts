/**
 * @file cardDefs.svelte.ts
 * @brief Client-side card kind table decoded from the `defs` match_event
 * packet.
 *
 * The backend emits `defs` (the full kind table) immediately followed by
 * `match_start` (which carries the digest of that table). This store keeps the
 * table in memory and exposes the two lookups the renderer needs:
 *
 *  - `(mod_index, kind_index) -> KindDef` — the `CompactCardV2` bit layout's
 *    identity indices (mod 8 bits / kind 12 bits / instance 12 bits).
 *  - `string_id -> KindDef` — the snapshot's `kind` string.
 *
 * `match_start.defs_digest` is compared against the ingested `defs.defs_digest`;
 * a mismatch drops the table rather than letting a stale face resolve.
 */

import { z } from "zod";
import { DefsPayloadSchema, MatchStartPayloadSchema } from "$lib/generated/schemas";

/** @brief Declarative face as serialized in a `defs` kind entry. */
export interface KindFace {
	/** Face kind token: `text` | `art_ref` | `emoji` | `blank`. */
	kind: string;
	/** `text` faces: the card's color. */
	color?: string;
	/** `text` / `emoji` faces: the value or glyph. */
	label?: string;
	/** `art_ref` faces: the art URL (placeholder in phase 1). */
	url?: string;
	/** Face content version — feeds the client face hash. */
	art_version?: number;
}

/** @brief One row of the frozen kind table, keyed both ways. */
export interface KindDef {
	/** Position in the frozen match mod list (CompactCardV2 bits 31..24). */
	mod_index: number;
	/** Position in the owning mod's sorted kind list (bits 23..12). */
	kind_index: number;
	/** Full frozen kind id, e.g. `vanilla:red_5`. */
	string_id: string;
	/** Declarative face, always present (normalized to `blank` if omitted). */
	face: KindFace;
	/** Content tags declared by the card definition. */
	tags: string[];
}

const RawFaceSchema = z.looseObject({
	kind: z.string().optional(),
	color: z.string().optional(),
	label: z.string().optional(),
	url: z.string().optional(),
	art_version: z.number().int().optional()
});

const RawKindSchema = z.looseObject({
	index: z.number().int(),
	string_id: z.string(),
	face: RawFaceSchema.optional(),
	tags: z.array(z.string()).optional()
});

const RawModSchema = z.looseObject({
	id: z.string(),
	index: z.number().int()
});

interface DefsTable {
	digest: string;
	byIndex: Map<string, KindDef>;
	byStringId: Map<string, KindDef>;
}

function indexKey(modIndex: number, kindIndex: number): string {
	return `${modIndex}:${kindIndex}`;
}

function normalizeFace(face: z.infer<typeof RawFaceSchema> | undefined): KindFace {
	if (!face) return { kind: "blank" };
	return {
		kind: face.kind ?? "blank",
		color: face.color,
		label: face.label,
		url: face.url,
		art_version: face.art_version
	};
}

/**
 * @brief Builds the lookup maps from a validated `defs` payload.
 *
 * The wire `kinds` array is grouped by mod in frozen mod-list order and each
 * entry carries only its per-mod `kind_index`, so the owning
 * `mod_index` is recovered from the mod-list entry whose `id` is the
 * namespace prefix of the kind's `string_id` (`defs_builder.hpp`). If a mod
 * entry is missing, the positional grouping is used as a fallback.
 */
function buildTable(payload: z.infer<typeof DefsPayloadSchema>): DefsTable {
	const byIndex = new Map<string, KindDef>();
	const byStringId = new Map<string, KindDef>();

	const modIndexByNamespace = new Map<string, number>();
	for (const rawMod of payload.mods) {
		const mod = RawModSchema.safeParse(rawMod);
		if (mod.success) modIndexByNamespace.set(mod.data.id, mod.data.index);
	}

	let runIndex = -1;
	let lastNamespace: string | null = null;
	for (const rawKind of payload.kinds) {
		const kind = RawKindSchema.safeParse(rawKind);
		if (!kind.success) continue;

		const stringId = kind.data.string_id;
		const separator = stringId.indexOf(":");
		const namespace = separator === -1 ? "" : stringId.slice(0, separator);

		let modIndex = modIndexByNamespace.get(namespace);
		if (modIndex === undefined) {
			if (namespace !== lastNamespace) runIndex += 1;
			modIndex = runIndex;
		} else {
			runIndex = modIndex;
		}
		lastNamespace = namespace;

		const def: KindDef = {
			mod_index: modIndex,
			kind_index: kind.data.index,
			string_id: stringId,
			face: normalizeFace(kind.data.face),
			tags: kind.data.tags ?? []
		};
		byIndex.set(indexKey(modIndex, kind.data.index), def);
		byStringId.set(stringId, def);
	}

	return { digest: payload.defs_digest, byIndex, byStringId };
}

/**
 * @class CardDefsStore
 * @brief Holds the decoded kind table and gates it on the `match_start`
 * digest. Lookups return `undefined` for anything not in the current table.
 */
class CardDefsStore {
	/** Digest of the active table, or null before a `defs` frame. */
	digest = $state<string | null>(null);

	/** True once a `match_start` has confirmed the active table's digest. */
	accepted = $state(false);

	#table: DefsTable | null = null;

	/** Latest `match_start` digest awaiting its `defs` frame (reordered packets). */
	#expectedDigest: string | null = null;

	/**
	 * @brief Ingests a `defs` packet and (re)builds the kind table.
	 *
	 * When a `match_start` was already seen, its digest must match or the
	 * packet is ignored; otherwise the table is held unconfirmed until
	 * `confirmMatchStart` runs.
	 * @returns True when the table was installed.
	 */
	ingestDefs(raw: unknown): boolean {
		const parsed = DefsPayloadSchema.safeParse(raw);
		if (!parsed.success) {
			this.#devWarn("defs payload failed validation — ignoring", parsed.error.message);
			return false;
		}

		const table = buildTable(parsed.data);
		const confirmed = this.#expectedDigest !== null && this.#expectedDigest === table.digest;
		if (this.#expectedDigest !== null && !confirmed) {
			this.#devWarn(
				"defs_digest does not match the active match_start — ignoring stale defs table"
			);
			return false;
		}

		this.#table = table;
		this.digest = table.digest;
		this.accepted = confirmed;
		if (confirmed) this.#expectedDigest = null;
		return true;
	}

	/**
	 * @brief Correlates a `match_start` with the ingested `defs` table.
	 *
	 * A matching digest confirms the table; a mismatch drops it so no face can
	 * resolve from a stale table. If `defs` has not arrived yet, the digest is
	 * remembered and validated against the next `defs` frame.
	 * @returns True when the currently held table is confirmed.
	 */
	confirmMatchStart(raw: unknown): boolean {
		const parsed = MatchStartPayloadSchema.safeParse(raw);
		if (!parsed.success) {
			this.#devWarn("match_start payload failed validation — cannot confirm defs table");
			return false;
		}
		const digest = parsed.data.defs_digest;

		if (this.#table === null) {
			this.#expectedDigest = digest;
			this.accepted = false;
			this.#devWarn("match_start arrived before defs — defs table not yet available");
			return false;
		}

		if (this.#table.digest !== digest) {
			this.#devWarn("defs_digest mismatch — dropping stale defs table");
			this.#expectedDigest = digest;
			this.#clearTable();
			return false;
		}

		this.accepted = true;
		this.#expectedDigest = null;
		return true;
	}

	/** @brief Looks up a kind by its `CompactCardV2` mod/kind indices. */
	lookupByIndex(modIndex: number, kindIndex: number): KindDef | undefined {
		return this.#table?.byIndex.get(indexKey(modIndex, kindIndex));
	}

	/** @brief Looks up a kind by its full frozen string id. */
	lookupByStringId(stringId: string): KindDef | undefined {
		return this.#table?.byStringId.get(stringId);
	}

	/** @brief Drops the table and any pending digest (match end / session reset). */
	reset(): void {
		this.#table = null;
		this.#expectedDigest = null;
		this.digest = null;
		this.accepted = false;
	}

	#clearTable(): void {
		this.#table = null;
		this.digest = null;
		this.accepted = false;
	}

	#devWarn(message: string, detail?: string): void {
		if (import.meta.env.DEV) {
			console.warn(`[cardDefs] ${message}${detail ? `: ${detail}` : ""}`);
		}
	}
}

export const storeCardDefs = new CardDefsStore();
