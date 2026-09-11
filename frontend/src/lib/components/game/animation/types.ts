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
}

/** Every step in a beat starts together; beats themselves play one after another. */
export type AnimationBeat = AnimationStep[];

export interface AnimationSequence {
	/** Schema version. Evolve additively — see contract/asyncapi.yaml. */
	version: number;
	beats: AnimationBeat[];
}
