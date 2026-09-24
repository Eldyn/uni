import type { AnimationStep } from "../types";
import type { RenderContext } from "../renderContext";
import { noopEffectHandle, type EffectHandle } from "../effectHandle";
import {
	resolveOutcome,
	outcomeKeyOf,
	type OutcomeDescriptor
} from "$components/game/entity/outcomeResolver";

/**
 * Spawns an entity in the cosmetic runtime and returns a persistent,
 * non-blocking handle — the entity lives in its owning subsystem until a
 * `despawnEntity` / its own `lifetime` ends it, so the beat never waits on it.
 * `skip()` despawns the entity unless it was spawned committed-persistent
 * (`payload.persistent`), so skipping a transient effect cleans it up.
 *
 * When the step carries the bound `$last_outcome` key (`payload.outcome` or
 * `payload.last_outcome.key`), the spawned entity's `outcome.resolver` selects
 * a descriptor for it; the descriptor is stored on the entity's
 * state as `state.outcome` for the owning subsystem to consume (audio/motion
 * selection), with the key always recorded as `state.outcomeKey`.
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

	const outcomeKey = outcomeKeyOf(step.payload);
	if (instanceId !== 0 && outcomeKey !== undefined) {
		const spawned = runtime.get(instanceId);
		const resolver = spawned?.components["outcome.resolver"] as
			Record<string, OutcomeDescriptor> | undefined;
		const descriptor = resolveOutcome(resolver, outcomeKey);
		runtime.setState(instanceId, {
			outcomeKey,
			...(descriptor ? { outcome: descriptor } : {})
		});
	}

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
