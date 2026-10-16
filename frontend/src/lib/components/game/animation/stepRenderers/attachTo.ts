import type { AnimationStep } from "../types";
import type { RenderContext } from "../renderContext";
import { noopEffectHandle, type EffectHandle } from "../effectHandle";

/** Registers the target entity under an anchor address (`payload.anchor`) so
 *  other entities' movement/motion refs resolve to it. Non-blocking. */
export function attachToRenderer(step: AnimationStep, ctx: RenderContext): EffectHandle {
	const runtime = ctx.entityRuntime;
	const entity = runtime?.resolve(step.target);
	if (!runtime || !entity) {
		console.warn(`attachTo step: target "${step.target}" did not resolve to an entity — skipped.`);
		return noopEffectHandle();
	}

	const anchor = step.payload?.anchor;
	if (typeof anchor !== "string") {
		console.warn(`attachTo step: no string payload.anchor for target "${step.target}" — skipped.`);
		return noopEffectHandle();
	}

	runtime.attachTo(entity.instanceId, anchor);
	return noopEffectHandle();
}
