/**
 * @file outcomeResolver.ts
 * @brief Client-side `outcome.resolver` selection.
 *
 * An entity def may declare an `outcome.resolver` component whose value is
 * `{ "<ns:key>": descriptor, "*": {} }`. When a `spawnEntity` step carries the
 * bound `$last_outcome` key, the renderer selects a descriptor by exact key,
 * then the `*` catch-all, then no-op — the same order the server validator's
 * coverage check guarantees will always terminate.
 *
 * Descriptors are deliberately loose (`{ animation?, sound?, motion?, model? }`):
 * stores the resolved descriptor on the entity's state for the owning
 * subsystem (a future entity renderer) to consume. Selection never
 * throws — a malformed resolver or descriptor degrades to `undefined` so a
 * mod-authored def cannot wedge the animation queue.
 */

/** The descriptor vocabulary; unknown keys are preserved verbatim. */
export interface OutcomeDescriptor {
	animation?: string;
	sound?: string;
	motion?: string;
	model?: string;
	[key: string]: unknown;
}

function isRecord(value: unknown): value is Record<string, unknown> {
	return typeof value === "object" && value !== null && !Array.isArray(value);
}

/**
 * @brief Selects the descriptor for `key`: exact match, then `*`, else
 * `undefined`. A non-object descriptor is treated as absent so a malformed
 * exact entry falls through to the catch-all rather than poisoning the result.
 */
export function resolveOutcome(
	resolver: Record<string, OutcomeDescriptor> | undefined,
	key: string
): OutcomeDescriptor | undefined {
	if (!isRecord(resolver)) return undefined;
	const exact = Object.prototype.hasOwnProperty.call(resolver, key) ? resolver[key] : undefined;
	if (isRecord(exact)) return exact as OutcomeDescriptor;
	const star = resolver["*"];
	return isRecord(star) ? (star as OutcomeDescriptor) : undefined;
}

/**
 * @brief Reads the bound `$last_outcome` key from a `spawnEntity` step's
 * payload. Accepts the emitted `{ outcome: "ns:key" }` form and the
 * `roll_table` binding shape `{ last_outcome: { key } }`. Absent/malformed
 * degrades to `undefined` (the entity still spawns, just unresolved).
 */
export function outcomeKeyOf(payload: unknown): string | undefined {
	if (!isRecord(payload)) return undefined;
	if (typeof payload.outcome === "string") return payload.outcome;
	const last = payload.last_outcome;
	if (isRecord(last) && typeof last.key === "string") return last.key;
	return undefined;
}
