import { gsap } from "gsap";
import type { AnimationStep } from "../types";
import type { RenderContext } from "../renderContext";
import { IMPACT_TUNING, MAX_HIT_STOP_MS, type ImpactProfile } from "../impactProfile";
import { cameraPunchOffset } from "../../layout/cameraRig";
import { storeAnimation } from "$stores/animation.svelte";
import { storeCameraOffset } from "$stores/cameraOffset.svelte";

const MIN_SPEED_MULTIPLIER = 0.1;

/** A special card's landing impact: hit-stop and camera punch.
 *  Plays nothing while the queue is fast-forwarding. */
export function impactRenderer(step: AnimationStep, ctx: RenderContext): gsap.core.Timeline {
	const timeline = gsap.timeline();
	const profile = step.payload?.profile as ImpactProfile | undefined;
	if (!profile || ctx.fastForwarding) return timeline;

	if (profile.hitStopMs > 0 && ctx.hitStop) {
		const speed = Math.max(MIN_SPEED_MULTIPLIER, storeAnimation.speedMultiplier);
		const durationMs = Math.min(MAX_HIT_STOP_MS, profile.hitStopMs / speed);
		const hitStop = ctx.hitStop;
		timeline.call(() => hitStop(durationMs, IMPACT_TUNING.hitStopTimeScale), undefined, 0);
	}

	if (profile.cameraPunch > 0) {
		const peak = cameraPunchOffset(profile.cameraPunch);
		const punch = { amount: 0 };
		const applyPunch = () =>
			storeCameraOffset.applyOffset({
				x: peak.x * punch.amount,
				y: peak.y * punch.amount,
				z: peak.z * punch.amount
			});
		timeline
			.to(
				punch,
				{
					amount: 1,
					duration: IMPACT_TUNING.punchAttackS,
					ease: "power2.out",
					onUpdate: applyPunch
				},
				0
			)
			.to(
				punch,
				{
					amount: 0,
					duration: IMPACT_TUNING.punchReleaseS,
					ease: "power2.inOut",
					onUpdate: applyPunch,
					onComplete: () => storeCameraOffset.clearOffset()
				},
				IMPACT_TUNING.punchAttackS
			);
	}

	return timeline;
}
