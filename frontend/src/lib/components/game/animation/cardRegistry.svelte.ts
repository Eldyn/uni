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

export class CardRegistry {
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
		const reactivePose = $state({ ...pose });
		this.#poses.set(cardId, reactivePose);
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
		const finish = this.#finishCurrentBeat;
		this.#currentTimeline?.progress(1, true);
		finish?.();
	}

	/** Fast-forwards every beat currently playing AND every batch still
	 *  waiting, without animating any of it, then drains the queue completely.
	 *  For when the tab goes into the background: nothing is being watched, so
	 *  there's no frame worth animating, and letting a whole backlog build up
	 *  silently just to replay it all at once the moment the tab comes back is
	 *  worse than snapping straight to the final state now. */
	flushImmediately(): void {
		let guard = 0;
		while ((this.#currentTimeline || this.#pending.length > 0) && guard++ < 10_000) {
			if (this.#currentTimeline) this.skipCurrent();
			else this.#pump();
		}
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
		try {
			const ctx: RenderContext = {
				getPose: (cardId, startPose) => {
					let pose = this.#poses.get(cardId);
					if (!pose) {
						const reactivePose = $state({ ...startPose });
						pose = reactivePose;
						this.#poses.set(cardId, pose);
					}
					if (!this.activeFlights.some((f) => f.id === cardId)) {
						const meta = this.#cardMeta.get(cardId) ?? { type: "wild", value: "0" };
						this.activeFlights = [...this.activeFlights, { id: cardId, pose, card: meta }];
					}
					return pose;
				},
				resolveAnchor: (name) => {
					const anchor = batch.anchors[name];
					if (!anchor) throw new Error(`CardRegistry: no anchor registered for "${name}"`);
					return anchor;
				}
			};

			const timeline = gsap.timeline();
			for (const step of beat) {
				const renderer = this.#registry[step.op];
				if (!renderer) {
					console.warn(`CardRegistry: unknown op "${step.op}" for target "${step.target}" — skipping.`);
					continue;
				}
				try {
					timeline.add(renderer(step, ctx), step.atS ?? 0);
				} catch (err) {
					console.error(
						`CardRegistry: renderer for op "${step.op}" target "${step.target}" threw — skipping step.`,
						err
					);
				}
			}
			// Read once per beat, not per tick — changing speed mid-flight only
			// takes effect starting the next beat. Acceptable: beats are short
			// (sub-second), so this isn't a real "changed nothing" bug.
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
				// A later beat in this SAME batch (e.g. the landing shake queued
				// right after a play's move+flip beat) can target the very same
				// card id. Retiring unconditionally here deletes both its pose AND
				// its registered meta the instant this beat ends — the next beat's
				// getPose then free-falls to the all-zero/dummy-meta fallback
				// (world origin, {type:"wild",value:"0"}), which is exactly the
				// stray "0" card that flashed center-screen between a play landing
				// and its shake. Only retire targets no later beat in this batch
				// still needs.
				for (const step of beat) {
					const usedLater = batch.beats
						.slice(beatIndex + 1)
						.some((laterBeat) => laterBeat.some((laterStep) => laterStep.target === step.target));
					if (!usedLater) this.#retireFlight(step.target);
				}
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
		} catch (err) {
			console.error(`CardRegistry: beat ${beatIndex} failed unexpectedly — advancing past it.`, err);
			this.#currentTimeline = null;
			this.#finishCurrentBeat = null;
			this.#playBatch(batch, beatIndex + 1);
		}
	}

	#retireFlight(cardId: string): void {
		this.#poses.delete(cardId);
		this.#cardMeta.delete(cardId);
		this.activeFlights = this.activeFlights.filter((f) => f.id !== cardId);
	}
}

const CARD_REGISTRY_KEY = Symbol("card-registry");

export function createCardRegistry(): CardRegistry {
	const queue = new CardRegistry();
	try {
		// setContext requires component initialisation; unit tests exercise the
		// queue directly (via the returned instance) without mounting a
		// component, so registering the context is best-effort here.
		setContext(CARD_REGISTRY_KEY, queue);
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
		// the returned instance rather than useCardRegistry().
	}
	return queue;
}

export function useCardRegistry(): CardRegistry {
	return getContext<CardRegistry>(CARD_REGISTRY_KEY);
}
