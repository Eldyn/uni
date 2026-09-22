import { gsap } from "gsap";
import type { AnimationStep } from "../types";
import type { RenderContext } from "../renderContext";

/** Reserved, not implemented: no EffectComposer pipeline exists yet.
 *  Resolves immediately so a sequence containing a screenEffect step
 *  doesn't stall the queue. */
export function screenEffectRenderer(step: AnimationStep, _ctx: RenderContext): gsap.core.Timeline {
	console.warn(
		`screenEffect step ("${step.payload?.effect ?? "unknown"}") not yet implemented — skipping.`
	);
	const timeline = gsap.timeline();
	// Add a dummy instant animation to give the timeline a measurable duration,
	// then complete it so progress() returns 1
	timeline.to({}, { duration: 0.001 });
	timeline.time(timeline.duration());
	return timeline;
}
