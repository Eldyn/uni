import type { AnimationStep } from "../types";
import type { RenderContext } from "../renderContext";
import { noopEffectHandle, type EffectHandle } from "../effectHandle";
import { storeAudio } from "$stores/audio.svelte";

/** Plays a one-shot SFX through the shared audio store's 2D player. Audio is
 *  fire-and-forget: the beat never waits on it, so the handle is non-blocking
 *  and its `skip()` is a no-op. */
export function emitSoundRenderer(step: AnimationStep, _ctx: RenderContext): EffectHandle {
	const sound = step.payload?.sound ?? step.payload?.id;
	if (typeof sound !== "string") {
		console.warn(
			`emitSound step for target "${step.target}" is missing a string payload.sound — skipped.`
		);
		return noopEffectHandle();
	}

	const opts: { pitch?: number; volume?: number } = {};
	if (typeof step.payload?.pitch === "number") opts.pitch = step.payload.pitch;
	if (typeof step.payload?.volume === "number") opts.volume = step.payload.volume;

	try {
		storeAudio.playSfx(sound, opts);
	} catch (err) {
		console.warn(`emitSound step: audio backend threw for "${sound}" — dropped.`, err);
	}
	return noopEffectHandle();
}
