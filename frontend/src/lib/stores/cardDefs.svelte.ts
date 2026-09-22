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
import type { FaceSlotVariant } from "$components/game/three/faceResolution";

/** @brief Declarative face as serialized in a `defs` kind entry. */
export interface KindFace {
	/** Face kind token: `text` | `image` | `emoji` | `blank`. */
	kind: string;
	/** `text` faces: the card's color. */
	color?: string;
	/** `text` / `emoji` faces: the value or glyph. */
	label?: string;
	/** Legacy placeholder art URL. */
	url?: string;
	/** `image` faces: the referenced asset bundle id. */
	art?: string;
	/** Layer composition. */
	art_mode?: "inset" | "replace" | "overlay";
	/** Art fit inside the target rect. */
	art_fit?: "contain" | "cover" | "stretch";
	/** Layers drawn over the art. */
	keep?: string[];
	/** Face content version — feeds the client face hash. */
	art_version?: number;
	/** Resolved asset variants per slot; file variants carry url+hash. */
	slots?: Record<string, FaceSlotVariant[]>;
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

const RawVariantSchema = z.looseObject({
	tier: z.enum(["high", "medium", "low"]),
	url: z.string().optional(),
	hash: z.string().optional(),
	value: z.string().optional()
});

const RawFaceSchema = z.looseObject({
	kind: z.string().optional(),
	color: z.string().optional(),
	label: z.string().optional(),
	url: z.string().optional(),
	art: z.string().optional(),
	art_mode: z.enum(["inset", "replace", "overlay"]).optional(),
	art_fit: z.enum(["contain", "cover", "stretch"]).optional(),
	keep: z.array(z.string()).optional(),
	art_version: z.number().int().optional(),
	slots: z.record(z.string(), z.array(RawVariantSchema)).optional()
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
		art: face.art,
		art_mode: face.art_mode,
		art_fit: face.art_fit,
		keep: face.keep,
		art_version: face.art_version,
		slots: face.slots
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

	/**
	 * True iff the active table's digest equals the last digest a `match_start`
	 * confirmed. Written by the validation gate, not merely by table presence:
	 * a bare `defs` installs unconfirmed (`accepted` false) until a matching
	 * `match_start` arrives.
	 */
	accepted = $state(false);

	#table: DefsTable | null = null;

	/**
	 * Last digest a `match_start` confirmed. Retained across later frames (not
	 * cleared on success) so a subsequent lone `defs` is validated against it;
	 * a conflicting digest is rejected outright.
	 */
	#confirmedDigest: string | null = null;

	/** Latest `match_start` digest awaiting its `defs` frame (reordered packets). */
	#pendingDigest: string | null = null;

	/**
	 * @brief Ingests a `defs` packet and, when validated, (re)builds the kind
	 * table.
	 *
	 * Validation (the table is only ever installed from a validated frame):
	 *  - a pending `match_start` digest must equal this frame's digest (the
	 *    reordered-packet case; a match confirms the table);
	 *  - otherwise, when a digest was already confirmed, this frame's digest
	 *    must equal it — a conflicting table is dropped with a WARN so it can
	 *    never become a lookup source;
	 *  - with no confirmation yet the first table installs unconfirmed, to be
	 *    validated by the `match_start` that follows.
	 * @returns True when the table was installed.
	 */
	ingestDefs(raw: unknown): boolean {
		const parsed = DefsPayloadSchema.safeParse(raw);
		if (!parsed.success) {
			this.#devWarn("defs payload failed validation — ignoring", parsed.error.message);
			return false;
		}

		const table = buildTable(parsed.data);

		if (this.#pendingDigest !== null) {
			if (table.digest !== this.#pendingDigest) {
				this.#devWarn(
					"defs_digest does not match the pending match_start — ignoring stale defs table"
				);
				return false;
			}
			this.#confirmedDigest = table.digest;
			this.#pendingDigest = null;
			this.#install(table);
			return true;
		}

		if (this.#confirmedDigest !== null && table.digest !== this.#confirmedDigest) {
			this.#devWarn("defs_digest conflicts with the confirmed digest — ignoring stale defs table");
			return false;
		}

		this.#install(table);
		return true;
	}

	/**
	 * @brief Correlates a `match_start` with the ingested `defs` table.
	 *
	 * A matching digest confirms the table (and is retained); a mismatch drops
	 * the table so no face can resolve from a stale one, then awaits the `defs`
	 * frame carrying the `match_start` digest. If `defs` has not arrived yet,
	 * the digest is remembered and validated against the next `defs` frame.
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
			this.#pendingDigest = digest;
			this.#syncAccepted();
			this.#devWarn("match_start arrived before defs — defs table not yet available");
			return false;
		}

		if (this.#table.digest !== digest) {
			this.#devWarn("defs_digest mismatch — dropping stale defs table");
			this.#pendingDigest = digest;
			this.#clearTable();
			return false;
		}

		this.#confirmedDigest = digest;
		this.#pendingDigest = null;
		this.#syncAccepted();
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

	/** @brief Drops the table, the confirmed digest and any pending digest. */
	reset(): void {
		this.#table = null;
		this.#confirmedDigest = null;
		this.#pendingDigest = null;
		this.digest = null;
		this.accepted = false;
	}

	#install(table: DefsTable): void {
		this.#table = table;
		this.digest = table.digest;
		this.#syncAccepted();
	}

	#syncAccepted(): void {
		this.accepted =
			this.#confirmedDigest !== null &&
			this.#table !== null &&
			this.#table.digest === this.#confirmedDigest;
	}

	#clearTable(): void {
		this.#table = null;
		this.digest = null;
		this.#syncAccepted();
	}

	#devWarn(message: string, detail?: string): void {
		if (import.meta.env.DEV) {
			console.warn(`[cardDefs] ${message}${detail ? `: ${detail}` : ""}`);
		}
	}
}

export const storeCardDefs = new CardDefsStore();
