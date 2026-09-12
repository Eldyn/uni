import { getContext, setContext } from "svelte";
import { gsap } from "gsap";
import type { AnimationBeat, AnimationStep } from "./types";
import type { FlightPose, RenderContext } from "./renderContext";
import { storeAnimation } from "$stores/animation.svelte";
import { moveRenderer } from "./stepRenderers/move";
import { flipRenderer } from "./stepRenderers/flip";
import { shakeRenderer } from "./stepRenderers/shake";
import { screenEffectRenderer } from "./stepRenderers/screenEffect";
import { materialEffectRenderer } from "./stepRenderers/materialEffect";

export type StepRenderer = (step: AnimationStep, ctx: RenderContext) => gsap.core.Timeline;

export interface FlightHandle {
	id: string;
	pose: FlightPose;
	card: { type: string; value: string };
}

interface PendingBatch {
	beats: AnimationBeat[];
	anchors: Record<string, [number, number, number]>;
	resolve: () => void;
}

export class AnimationQueue {
	#registry: Record<string, StepRenderer> = {
		move: moveRenderer,
		flip: flipRenderer,
		shake: shakeRenderer,
		screenEffect: screenEffectRenderer,
		materialEffect: materialEffectRenderer
	};

	#cardMeta = new Map<string, { type: string; value: string }>();
	#poses = new Map<string, FlightPose>();
	#pending: PendingBatch[] = [];
	#currentTimeline: gsap.core.Timeline | null = null;
	#finishCurrentBeat: (() => void) | null = null;
	#playing = false;

	activeFlights = $state<FlightHandle[]>([]);

	/** Test/observability hook: called with the 0-based index of each beat
	 *  within its enqueue() batch as it completes (naturally or via skip). */
	onBeatComplete: ((index: number) => void) | null = null;

	registerCardMeta(cardId: string, card: { type: string; value: string }): void {
		this.#cardMeta.set(cardId, card);
	}

	/** Pre-seeds a card's flight pose before it is first requested via
	 *  getPose, so a beat's move step starts from its real current position
	 *  (e.g. its hand slot) instead of getPose's own startPose fallback,
	 *  which only applies the first time a card id is ever seen. */
	seedPose(cardId: string, pose: FlightPose): void {
		this.#poses.set(cardId, { ...pose });
	}

	/** Queues a batch of beats. `anchors` supplies every named world position
	 *  this batch's steps may reference via resolveAnchor. Returns a promise
	 *  resolving once every beat in the batch has completed (or been
	 *  skipped) — callers that need "wait for this to finish" (e.g. a
	 *  reshuffle before the next turn) await it directly. */
	enqueue(
		beats: AnimationBeat[],
		anchors: Record<string, [number, number, number]>
	): Promise<void> {
		return new Promise((resolve) => {
			this.#pending.push({ beats, anchors, resolve });
			this.#pump();
		});
	}

	skipCurrent(): void {
		this.#currentTimeline?.progress(1, true);
		this.#finishCurrentBeat?.();
	}

	#pump(): void {
		if (this.#playing) return;
		const batch = this.#pending[0];
		if (!batch) return;
		this.#playing = true;
		this.#playBatch(batch, 0);
	}

	#playBatch(batch: PendingBatch, beatIndex: number): void {
		if (beatIndex >= batch.beats.length) {
			this.#pending.shift();
			this.#playing = false;
			batch.resolve();
			this.#pump();
			return;
		}

		const beat = batch.beats[beatIndex];
		const ctx: RenderContext = {
			getPose: (cardId, startPose) => {
				if (!this.#poses.has(cardId)) {
					const pose = { ...startPose };
					this.#poses.set(cardId, pose);
					const meta = this.#cardMeta.get(cardId) ?? { type: "wild", value: "0" };
					this.activeFlights = [...this.activeFlights, { id: cardId, pose, card: meta }];
				}
				return this.#poses.get(cardId)!;
			},
			resolveAnchor: (name) => {
				const anchor = batch.anchors[name];
				if (!anchor) throw new Error(`AnimationQueue: no anchor registered for "${name}"`);
				return anchor;
			}
		};

		const timeline = gsap.timeline();
		for (const step of beat) {
			const renderer = this.#registry[step.op];
			if (!renderer) {
				console.warn(`AnimationQueue: unknown op "${step.op}" for target "${step.target}" — skipping.`);
				continue;
			}
			timeline.add(renderer(step, ctx), 0);
		}
		timeline.timeScale(storeAnimation.speedMultiplier);
		this.#currentTimeline = timeline;

		// GSAP's suppressEvents (passed by skipCurrent's progress(1, true)) suppresses
		// ALL callbacks, including onComplete — confirmed by direct experiment in an
		// earlier run. finishBeat must therefore be reachable from BOTH
		// a natural onComplete firing AND an explicit call from skipCurrent, guarded
		// so it only ever runs once per beat.
		let settled = false;
		const finishBeat = () => {
			if (settled) return;
			settled = true;
			for (const step of beat) this.#retireFlight(step.target);
			this.onBeatComplete?.(beatIndex);
			this.#currentTimeline = null;
			this.#finishCurrentBeat = null;
			this.#playBatch(batch, beatIndex + 1);
		};

		timeline.eventCallback("onComplete", finishBeat);
		this.#finishCurrentBeat = finishBeat;

		if (!storeAnimation.enabled) {
			timeline.progress(1, true);
			finishBeat();
		}
	}

	#retireFlight(cardId: string): void {
		this.#poses.delete(cardId);
		this.activeFlights = this.activeFlights.filter((f) => f.id !== cardId);
	}
}

const ANIMATION_QUEUE_KEY = Symbol("animation-queue");

export function createAnimationQueue(): AnimationQueue {
	const queue = new AnimationQueue();
	try {
		// setContext requires component initialisation; unit tests exercise the
		// queue directly (via the returned instance) without mounting a
		// component, so registering the context is best-effort here.
		setContext(ANIMATION_QUEUE_KEY, queue);
	} catch (err) {
		// Svelte 5's setContext throws when called outside component initialization.
		// The error message is a URL pointing to the lifecycle_outside_component
		// documentation. Only swallow that specific error; rethrow anything else.
		if (
			!(err instanceof Error) ||
			!err.message.includes("lifecycle_outside_component")
		) {
			throw err;
		}
		// Not inside component initialisation — fine for callers that only use
		// the returned instance rather than useAnimationQueue().
	}
	return queue;
}

export function useAnimationQueue(): AnimationQueue {
	return getContext<AnimationQueue>(ANIMATION_QUEUE_KEY);
}
