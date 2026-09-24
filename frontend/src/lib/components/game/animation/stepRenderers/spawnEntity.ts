import type { AnimationStep } from "../types";
import type { RenderContext } from "../renderContext";
import { noopEffectHandle, type EffectHandle } from "../effectHandle";

/**
 * Spawns an entity in the cosmetic runtime and returns a persistent,
 * non-blocking handle — the entity lives in its owning subsystem until a
 * `despawnEntity` / its own `lifetime` ends it, so the beat never waits on it.
 * `skip()` despawns the entity unless it was spawned committed-persistent
 * (`payload.persistent`), so skipping a transient effect cleans it up.
 */
export function spawnEntityRenderer(step: AnimationStep, ctx: RenderContext): EffectHandle {
	const runtime = ctx.entityRuntime;
	const entity = step.payload?.entity;
	if (typeof entity !== "string") {
		console.warn(
			`spawnEntity step for target "${step.target}" is missing a string payload.entity — skipped.`
		);
		return noopEffectHandle();
	}
	if (!runtime) {
		console.warn(`spawnEntity step: no EntityRuntime on the render context — skipped.`);
		return noopEffectHandle();
	}

	const as = typeof step.payload?.as === "string" ? step.payload.as : undefined;
	const instanceId = runtime.spawn(entity, { as });
	const committed = step.payload?.persistent === true;
	let despawned = false;

	return {
		persistent: true,
		blocking: null,
		skip(): void {
			if (committed || instanceId === 0 || despawned) return;
			despawned = true;
			runtime.despawn(instanceId);
		}
	};
}
