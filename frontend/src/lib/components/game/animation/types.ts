/**
 * @file types.ts
 * @brief Hand-written TypeScript mirror of the AnimationSequence/AnimationStep
 * schema documented in contract/asyncapi.yaml. Not code-generated — see that
 * file's comment for why (server-push shapes in this codebase are hand-typed
 * client-side, matching e.g. ListedLobby for LobbyList).
 */

export interface AnimationStep {
	/** Renderer key looked up in the StepRenderer registry. Unknown values
	 *  must be tolerated (no-op), never thrown on — see animationQueue.svelte.ts. */
	op: string;
	/** Card id, player/seat username, or the literal "screen". */
	target: string;
	/** Op-specific fields, shape depends on `op`. */
	payload?: Record<string, unknown>;
	/** Seconds into the beat's own timeline this step starts at (default 0,
	 *  i.e. together with every other step in the beat). Used to stagger
	 *  multiple cards within a single beat — e.g. a multi-card draw — without
	 *  losing the "beat N+1 waits for beat N" guarantee a real separate beat
	 *  per card would give up. */
	atS?: number;
}

/** Every step in a beat starts together; beats themselves play one after another. */
export type AnimationBeat = AnimationStep[];

export interface AnimationSequence {
	/** Schema version. Evolve additively — see contract/asyncapi.yaml. */
	version: number;
	beats: AnimationBeat[];
}
