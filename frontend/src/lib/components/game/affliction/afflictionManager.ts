/**
 * @file afflictionManager.ts
 * @brief Activation/expiry owner for active afflictions.
 *
 * The manager keeps the set of active afflictions, enforces activation-time
 * `exclusive` rejection so exclusive losers never accumulate,
 * and recomputes the composition map on every change. It is the single writer of
 * presentation state; consumers (`OverrideChannel`, T8 triggers, T9 resolver)
 * only read.
 *
 * Pure `.ts`, no runes, no game-state access. Unknown kinds/targets degrade to
 * `undefined` and never throw.
 */

import {
	compose,
	compareAfflictions,
	carriesExclusiveKind,
	KindModes,
	type ActiveAffliction,
	type ComposeCaps,
	type MergeMode
} from "./compose";
import { DEFAULT_FX_BUDGET } from "$stores/fxBudget.svelte";

/** Constructor options; both default to the engine table + budget defaults. */
export interface AfflictionManagerOptions {
	modes?: Record<string, MergeMode>;
	caps?: Partial<ComposeCaps>;
}

export class AfflictionManager {
	#active: ActiveAffliction[] = [];
	#seq = 0;
	#modes: Record<string, MergeMode>;
	#caps: ComposeCaps;
	#composition: Map<string, unknown> = new Map();

	constructor(opts: AfflictionManagerOptions = {}) {
		this.#modes = opts.modes ?? KindModes;
		this.#caps = {
			maxAfflictions: opts.caps?.maxAfflictions ?? DEFAULT_FX_BUDGET.maxAfflictions,
			screenStackMax: opts.caps?.screenStackMax ?? DEFAULT_FX_BUDGET.screenStackMax
		};
	}

	/** The active set, in activation order. Read-only view. */
	get active(): ActiveAffliction[] {
		return this.#active;
	}

	/**
	 * Activate an affliction. On a target that carries any `exclusive` kind the
	 * target is single-owner: the max-ranked affliction holds and the rest are
	 * *rejected at activation* (never stored), unlike `override` losers which
	 * remain active so expiry is reversible.
	 *
	 * `activationSeq` is engine-owned: the manager always stamps the monotonic
	 * activation counter so tie-breaks reflect real activation order.
	 */
	activate(a: ActiveAffliction): void {
		const norm: ActiveAffliction = {
			...a,
			priority: Number.isFinite(a.priority) ? a.priority : 0,
			activationSeq: this.#seq++,
			modifiers: Array.isArray(a.modifiers) ? a.modifiers : []
		};

		// Re-activating the same presentation replaces its prior instance rather
		// than stacking a duplicate. A repeated `defs` frame (reconnect or a
		// duplicate packet) re-installs every always-on presentation, and without
		// this the active set would grow without bound.
		const existing = this.#active.findIndex((x) => x.presentationId === norm.presentationId);
		if (existing !== -1) this.#active.splice(existing, 1);

		const onTarget = this.#active.filter((x) => x.target === norm.target);
		const group = [...onTarget, norm];

		if (group.some((x) => carriesExclusiveKind(x, this.#modes))) {
			let owner: ActiveAffliction | undefined;
			for (const x of group) if (owner === undefined || compareAfflictions(x, owner) > 0) owner = x;
			if (owner !== norm) return; // rejected at activation
			this.#active = this.#active.filter((x) => x.target !== norm.target);
		}

		this.#active.push(norm);
		this.#recompute();
	}

	/** Remove every active affliction with this presentation id. */
	expire(presentationId: string): void {
		const before = this.#active.length;
		this.#active = this.#active.filter((a) => a.presentationId !== presentationId);
		if (this.#active.length !== before) this.#recompute();
	}

	/**
	 * @brief Drop every active affliction and recompute the (now empty)
	 * composition. Called at match teardown so a finished match's active
	 * `face.override` (or any other presentation) cannot colour the next match.
	 */
	clear(): void {
		if (this.#active.length === 0) return;
		this.#active = [];
		this.#recompute();
	}

	/** The resolved result for `kindTarget(kind, target)`, or `undefined`. */
	resolve(kindTarget: string): unknown {
		return this.#composition.get(kindTarget);
	}

	#recompute(): void {
		this.#composition = compose(this.#active, this.#modes, this.#caps);
	}
}
