/**
 * @file tableSpin.svelte.ts
 * @brief Owns which player's POV the board is currently rendered from, for both
 * the local player (themselves) and a spectator (the viewed player).
 *
 * This used to be an animated controller: the whole table physically spun to
 * the incoming seat over a GSAP timeline, then blended the incoming/outgoing
 * hands in. That spin has been removed — a spectator POV switch is now a hard
 * cut, covered by storeSpectator's full-screen darken/fade, so the board simply
 * changes shape under the black. The spin state below (`phase`,
 * `boardRotationY`, `inheritProgress`, `transition`) is retained as a frozen
 * identity so the render layer (Scene3D's table group, AllCards3D's table-bound
 * pose fold, baseBeats' anchors) keeps a single, always-zero source of board
 * rotation instead of each re-deriving "no rotation" independently.
 */

/** @deprecated The spin is gone; kept so existing consumers/tests still type. */
export type SpinPhase = "idle" | "spin" | "inherit";

/** @deprecated The hand morph is gone; kept for consumers that still accept it. */
export interface HandMorph {
	/** Player the morph belongs to. */
	username: string;
	/** World pose per card index. */
	poses: [number, number, number][];
	/** Source in-plane orientation per card index, degrees. */
	spinDegs?: number[];
	/** Whether the morphed cards end face-up. */
	open: boolean;
}

/** @deprecated The spin transition is gone; kept so consumers still type. */
export interface SpinTransition {
	order: readonly string[];
	from: string;
	to: string;
	steps: number;
	spinAngle: number;
	incoming: HandMorph | null;
	outgoing: HandMorph | null;
}

class StoreTableSpin {
	/** The player whose POV is committed and currently drawn. */
	renderPov = $state<string | null>(null);
	/** The resolved POV target (same as renderPov now that commits are instant). */
	targetPov = $state<string | null>(null);
	/** Always "idle" — no spin phases exist any more. */
	phase = $state<SpinPhase>("idle");
	/** Always 0 — the board is never rotated. Read by the pose fold/anchor
	 *  resolvers so a canonical, unrotated frame is still published. */
	boardRotationY = $state(0);
	/** Always 1 — no morph to blend. */
	inheritProgress = $state(1);
	/** Always null — no in-flight transition. */
	transition = $state<SpinTransition | null>(null);

	get active(): boolean {
		return false;
	}

	/** Commits the requested POV immediately. Signature retained for the
	 *  render layer's call sites; the order/spin/morph arguments are ignored. */
	syncTarget(
		target: string | null,
		_order: readonly string[] = [],
		_spinAngle = 0,
		_incoming: HandMorph | null = null,
		_outgoing: HandMorph | null = null
	): void {
		this.targetPov = target;
		this.renderPov = target;
	}

	/** No-op: there is no in-flight transition to skip. */
	skip(): void {}

	/** Instantly commits the target POV. */
	cancelAndCommit(): void {
		if (this.targetPov !== null) this.renderPov = this.targetPov;
	}

	reset(): void {
		this.renderPov = null;
		this.targetPov = null;
		this.phase = "idle";
		this.boardRotationY = 0;
		this.inheritProgress = 1;
		this.transition = null;
	}
}

export const storeTableSpin = new StoreTableSpin();
