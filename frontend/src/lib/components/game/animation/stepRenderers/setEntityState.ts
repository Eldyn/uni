import type { AnimationStep } from "../types";
import type { RenderContext } from "../renderContext";
import { noopEffectHandle, type EffectHandle } from "../effectHandle";

/** Patches the target entity's runtime state (`payload.patch`, or `payload`
 *  itself as a fallback for a bare field map). Non-blocking; an unresolvable
 *  target or missing patch degrades with a warning. */
export function setEntityStateRenderer(step: AnimationStep, ctx: RenderContext): EffectHandle {
	const runtime = ctx.entityRuntime;
	const entity = runtime?.resolve(step.target);
	if (!runtime || !entity) {
		console.warn(
			`setEntityState step: target "${step.target}" did not resolve to an entity — skipped.`
		);
		return noopEffectHandle();
	}

	const patch = (step.payload?.patch ?? step.payload?.state) as Record<string, unknown> | undefined;
	if (!patch || typeof patch !== "object") {
		console.warn(`setEntityState step: no payload.patch for target "${step.target}" — skipped.`);
		return noopEffectHandle();
	}

	runtime.setState(entity.instanceId, patch);
	return noopEffectHandle();
}
