import type { AnimationStep } from "../types";
import type { RenderContext } from "../renderContext";
import { noopEffectHandle, type EffectHandle } from "../effectHandle";

/** Despawns the entity the step targets (`entity:<handle>` or an anchored
 *  address). Non-blocking; an unresolvable target degrades with a warning. */
export function despawnEntityRenderer(step: AnimationStep, ctx: RenderContext): EffectHandle {
	const runtime = ctx.entityRuntime;
	const entity = runtime?.resolve(step.target);
	if (!runtime || !entity) {
		console.warn(
			`despawnEntity step: target "${step.target}" did not resolve to an entity — skipped.`
		);
		return noopEffectHandle();
	}
	runtime.despawn(entity.instanceId);
	return noopEffectHandle();
}
