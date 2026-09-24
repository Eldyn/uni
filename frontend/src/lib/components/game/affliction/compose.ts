/**
 * @file compose.ts
 * @brief Pure affliction composition engine.
 *
 * An *affliction* is a client-side presentation modifier bound to a target
 * (`hand`, `discard`, `table`, `card:<id>`, `seat:<user>` or `entity:<id>`).
 * Several afflictions may be active at once; this module folds them into a
 * resolved map that the render/audio consumers read.
 *
 * Two stages:
 *   1. Target-level `exclusive` — if any affliction on a target carries an
 *      exclusive kind, that target is single-owner: the max
 *      (priority, activationSeq, presentationId) holds and every other
 *      affliction on the target is dropped.
 *   2. Per-(kind, target) merge for the survivors — `override` keeps the
 *      highest-ranked modifier (losers stay active, so expiry is reversible);
 *      `additive` keeps all of them in priority order, capped, dropping the
 *      lowest.
 *
 * The engine owns the kind -> merge-mode table; mod content never carries
 * a `mode`. Unknown kinds/modes degrade to `override` and never throw, so a
 * mod-authored presentation cannot wedge the queue (degrade-don't-die).
 *
 * Pure `.ts`, no runes, no game-state access: cosmetic randomness and
 * presentation state never leak into `storeGame.state`.
 */

/** How multiple active modifiers of one kind on one target combine. */
export type MergeMode = "override" | "additive" | "exclusive";

/** One `{ kind, ...params }` modifier declared by a presentation. */
export interface AfflictionModifier {
	kind: string;
	[key: string]: unknown;
}

/** A live affliction instance. `expiry` is a marker owned by the
 *  trigger layer (T8); `null`/absent means persistent. */
export interface ActiveAffliction {
	presentationId: string;
	target: string;
	priority: number;
	activationSeq: number;
	modifiers: AfflictionModifier[];
	expiry?: number | null;
}

/** Engine-owned kind -> merge-mode table. The server validates against
 *  the same vocabulary fail-closed; the client only classifies. */
export const KindModes: Record<string, MergeMode> = {
	"face.override": "override",
	"material.param": "override",
	"screen.effect": "additive",
	"particles.emitter": "additive",
	"audio.loop": "exclusive"
};

/** Additive budgets; mirror `fxBudget.svelte.ts` defaults. Injectable so tests
 *  and the caller can pin them (the store can override at runtime). */
export interface ComposeCaps {
	maxAfflictions: number;
	screenStackMax: number;
}

const DEFAULT_CAPS: ComposeCaps = { maxAfflictions: 8, screenStackMax: 3 };

/** `screen.effect` stacks share the smaller screen budget; every other additive
 *  kind uses the affliction budget. */
function capFor(kind: string, caps: ComposeCaps): number {
	return kind === "screen.effect" ? caps.screenStackMax : caps.maxAfflictions;
}

/**
 * The composition key for a resolved result: the effective result of `kind` on
 * `target`. `AfflictionManager.resolve` takes this exact string.
 */
export function kindTarget(kind: string, target: string): string {
	return `${kind}@${target}`;
}

/** Rank order: higher priority, then later activation, then lexicographic id.
 *  Returns > 0 when `a` outranks `b`. Shared with `AfflictionManager` so
 *  activation-time exclusivity uses the exact composition order. */
export function compareAfflictions(a: ActiveAffliction, b: ActiveAffliction): number {
	if (a.priority !== b.priority) return a.priority - b.priority;
	if (a.activationSeq !== b.activationSeq) return a.activationSeq - b.activationSeq;
	if (a.presentationId < b.presentationId) return -1;
	if (a.presentationId > b.presentationId) return 1;
	return 0;
}

function modeOf(modes: Record<string, MergeMode>, kind: string): MergeMode {
	return modes[kind] ?? "override";
}

/** True when any modifier of `a` carries an exclusive kind. */
export function carriesExclusiveKind(
	a: ActiveAffliction,
	modes: Record<string, MergeMode>
): boolean {
	return (a.modifiers ?? []).some((m) => modeOf(modes, m.kind) === "exclusive");
}

/**
 * @brief Fold active afflictions into the resolved map keyed by
 * `kindTarget(kind, target)`.
 *
 * Recomputed by the manager on every activation/expiry. Handles homogeneous or
 * mixed-target lists; keys always encode both kind and target so the same kind
 * on different targets never collides.
 */
export function compose(
	active: ActiveAffliction[],
	modes: Record<string, MergeMode>,
	caps: ComposeCaps = DEFAULT_CAPS
): Map<string, unknown> {
	const out = new Map<string, unknown>();

	const byTarget = new Map<string, ActiveAffliction[]>();
	for (const a of active) {
		const list = byTarget.get(a.target);
		if (list) list.push(a);
		else byTarget.set(a.target, [a]);
	}

	for (const [target, group] of byTarget) {
		// Stage 1: target-level exclusive — one owner holds, all others drop.
		if (group.some((a) => carriesExclusiveKind(a, modes))) {
			let owner: ActiveAffliction | undefined;
			for (const a of group) if (owner === undefined || compareAfflictions(a, owner) > 0) owner = a;
			if (owner) {
				for (const m of owner.modifiers ?? []) out.set(kindTarget(m.kind, target), m);
			}
			continue;
		}

		// Stage 2: per-(kind, target) merge among the survivors.
		const byKind = new Map<
			string,
			{ affliction: ActiveAffliction; modifier: AfflictionModifier }[]
		>();
		for (const a of group) {
			for (const m of a.modifiers ?? []) {
				const list = byKind.get(m.kind);
				if (list) list.push({ affliction: a, modifier: m });
				else byKind.set(m.kind, [{ affliction: a, modifier: m }]);
			}
		}

		for (const [kind, entries] of byKind) {
			const ranked = [...entries].sort((x, y) => compareAfflictions(y.affliction, x.affliction));
			if (modeOf(modes, kind) === "additive") {
				const cap = Math.max(0, Math.floor(capFor(kind, caps)));
				out.set(
					kindTarget(kind, target),
					ranked.slice(0, cap).map((e) => e.modifier)
				);
			} else {
				// override (and any unknown kind, which degrades to override)
				out.set(kindTarget(kind, target), ranked[0]?.modifier);
			}
		}
	}

	return out;
}
