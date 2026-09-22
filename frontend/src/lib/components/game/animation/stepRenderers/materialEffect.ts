import { gsap } from "gsap";
import type { AnimationStep } from "../types";
import type { RenderContext } from "../renderContext";

/** Reserved, not implemented: no custom ShaderMaterial swap path exists yet.
 *  Resolves immediately so a sequence containing a materialEffect step
 *  doesn't stall the queue. */
export function materialEffectRenderer(
	step: AnimationStep,
	_ctx: RenderContext
): gsap.core.Timeline {
	console.warn(`materialEffect step on target "${step.target}" not yet implemented — skipping.`);
	const timeline = gsap.timeline();
	// Add a dummy instant animation to give the timeline a measurable duration,
	// then complete it so progress() returns 1
	timeline.to({}, { duration: 0.001 });
	timeline.time(timeline.duration());
	return timeline;
}
