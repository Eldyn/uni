/**
 * @file effectHandle.ts
 * @brief The renderer result contract. A renderer returns
 * either a bare GSAP timeline — the historical shape, blocking beat advance —
 * or an EffectHandle that can opt out of blocking and/or expose its own skip
 * behaviour. `normalizeStepResult` wraps the bare-timeline form so `#playBatch`
 * has one uniform shape to reason about without churning the five existing
 * renderers.
 */
import type { gsap } from "gsap";

export interface EffectHandle {
	/** Fast-forward this effect to its end state. Must never throw. */
	skip(): void;
	/** The timeline beat advance waits on, or null to resolve immediately. */
	blocking: gsap.core.Timeline | null;
	/** True when the effect detaches to its owning subsystem and is not awaited. */
	persistent: boolean;
}

export type StepResult = gsap.core.Timeline | EffectHandle;

export function isEffectHandle(result: StepResult): result is EffectHandle {
	return (
		typeof (result as EffectHandle).skip === "function" &&
		"blocking" in result &&
		typeof (result as EffectHandle).persistent === "boolean"
	);
}

/** Wraps a bare Timeline as a non-persistent blocking handle. */
export function normalizeStepResult(result: StepResult): EffectHandle {
	if (isEffectHandle(result)) return result;
	return {
		skip: () => {
			result.progress(1, true);
		},
		blocking: result,
		persistent: false
	};
}

/** A handle for a step that neither blocks nor has anything to skip. */
export function noopEffectHandle(): EffectHandle {
	return { skip: () => {}, blocking: null, persistent: false };
}
