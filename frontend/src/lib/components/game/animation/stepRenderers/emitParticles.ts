import { gsap } from "gsap";
import type { AnimationStep } from "../types";
import type { RenderContext } from "../renderContext";

/** Reserved, not implemented: no particle emitter subsystem exists yet. Tolerated stub — warns and resolves immediately so a sequence
 *  containing an emitParticles step never stalls or throws into the queue. */
export function emitParticlesRenderer(
	step: AnimationStep,
	_ctx: RenderContext
): gsap.core.Timeline {
	console.warn(
		`emitParticles step ("${step.payload?.emitter ?? step.payload?.particles ?? "unknown"}") not yet implemented — skipping.`
	);
	const timeline = gsap.timeline();
	// A zero-duration timeline has no measurable duration; add an instant dummy
	// then complete it so progress() reads 1, matching the screenEffect stub.
	timeline.to({}, { duration: 0.001 });
	timeline.time(timeline.duration());
	return timeline;
}
