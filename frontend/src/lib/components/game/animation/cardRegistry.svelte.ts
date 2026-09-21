import { getContext, setContext, untrack } from "svelte";
import { SvelteSet } from "svelte/reactivity";
import { gsap } from "gsap";
import type { AnimationBeat, AnimationStep } from "./types";
import { createDefaultFlightPose, type FlightPose, type RenderContext } from "./renderContext";
import { storeAnimation } from "$stores/animation.svelte";
import { moveRenderer } from "./stepRenderers/move";
import { flipRenderer } from "./stepRenderers/flip";
import { shakeRenderer } from "./stepRenderers/shake";
import { screenEffectRenderer } from "./stepRenderers/screenEffect";
import { materialEffectRenderer } from "./stepRenderers/materialEffect";

export type StepRenderer = (step: AnimationStep, ctx: RenderContext) => gsap.core.Timeline;

export interface CardDecoration {
	hovered?: boolean;
	instant?: boolean;
	hoverPush?: [number, number];
	pushX?: number;
	hoverSpinDeg?: number;
	opacity?: number;
	dimmed?: boolean;
	/** Ambient brightness multiplier (0-1) — inter-card ambient occlusion, e.g.
	 *  darker cards deeper in a stack. Defaults to 1 when unset. */
	brightness?: number;
	/** True when this card belongs to the rotating TABLE (a ring, discard or
	 *  draw-pile card) rather than to the viewer's own hand row. AllCards3D
	 *  folds the spectator-spin board yaw into the pose only for table-bound
	 *  cards — see animation/cardBoardPose.ts. */
	tableBound?: boolean;
	shadow?: { texture: import("three").Texture; offsetX: number; dropZ: number; opacity: number };
	highlight?: { color?: string; pulse?: boolean };
}

export interface CardMeta {
	type: string;
	value: string;
	wildColor?: import("$stores/game.svelte").CardType;
}

export interface FlightHandle {
	id: string;
	pose: FlightPose;
	card: CardMeta;
	decoration?: CardDecoration;
}

interface PendingBatch {
	beats: AnimationBeat[];
	resolveAnchor: (name: string) => [number, number, number];
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

	#cardMeta = new Map<string, CardMeta>();
	#poses = new Map<string, FlightPose>();
	#poseProviders = new Map<string, () => [number, number, number]>();
	#inTransitIds = new SvelteSet<string>();
	#flightHandles = new Map<string, FlightHandle>();
	#decorations = new Map<string, CardDecoration | undefined>();
	#prevHoveredMap = new Map<string, boolean>();
	#liftTweens = new Map<string, gsap.core.Tween>();
	#punchTimelines = new Map<string, gsap.core.Timeline>();
	#pending: PendingBatch[] = [];
	#currentTimeline: gsap.core.Timeline | null = null;
	#finishCurrentBeat: (() => void) | null = null;
	#playing = false;

	activeFlights = $state<FlightHandle[]>([]);

	/** Test/observability hook: called with the 0-based index of each beat
	 *  within its enqueue() batch as it completes (naturally or via skip). */
	onBeatComplete: ((index: number) => void) | null = null;

	registerCardMeta(cardId: string, card: CardMeta): void {
		this.#cardMeta.set(cardId, card);
		const handle = this.#flightHandles.get(cardId);
		if (handle) {
			handle.card = card;
		}
	}

	/** Pre-seeds a card's flight pose before it is first requested via
	 *  getPose, so a beat's move step starts from its real current position
	 *  (e.g. its hand slot) instead of getPose's own startPose fallback,
	 *  which only applies the first time a card id is ever seen.
	 *
	 *  Mutates an existing pose object in place rather than replacing it —
	 *  an owner's registration effect (LocalHand3D/DiscardPile3D's
	 *  ensureEntry, Tasks A10/A11) can race this call and create the pose
	 *  entry first; if seedPose swapped in a brand-new object here, any
	 *  activeFlights entry already pointing at the old one would go stale
	 *  and never reflect the seeded — or subsequently GSAP-tweened — pose,
	 *  regardless of which effect happened to run first. */
	seedPose(
		cardId: string,
		pose: Partial<FlightPose> & { x: number; y: number; z: number }
	): FlightPose;
	seedPose(cardId: string, pose: FlightPose): void;
	seedPose(cardId: string, pose: any): any {
		return untrack(() => {
			const existing = this.#poses.get(cardId);
			if (existing) {
				Object.assign(existing, pose);
				return existing;
			}
			const fullPose = createDefaultFlightPose(pose);
			const reactivePose = $state(fullPose);
			this.#poses.set(cardId, reactivePose);
			return reactivePose;
		});
	}

	/** Registers (or clears, passing null) the pure function an owner
	 *  (LocalHand3D, DiscardPile3D) uses to compute this card's CURRENT idle
	 *  pose. Called by the owner every time its own layout recomputes — see
	 *  applyIdlePoseIfNotInTransit. Retiring a transition for this card id
	 *  reads this provider (if any) instead of deleting the pose outright,
	 *  which is what makes a flight-to-idle handoff a no-op rather than a
	 *  1-frame pop. */
	setPoseProvider(cardId: string, provider: (() => [number, number, number]) | null): void {
		if (provider) this.#poseProviders.set(cardId, provider);
		else this.#poseProviders.delete(cardId);
	}

	/** True while a GSAP timeline is actively tweening this card's pose — an
	 *  owner's own idle-pose sync (applyIdlePoseIfNotInTransit) must never
	 *  stomp a pose GSAP currently owns. */
	isInTransit(cardId: string): boolean {
		return this.#inTransitIds.has(cardId);
	}

	/** Explicitly mark a card as in-transit or idle (e.g. during hand drag/displacement tweens). */
	markInTransit(cardId: string, inTransit: boolean): void {
		if (inTransit) {
			this.#inTransitIds.add(cardId);
		} else {
			this.#inTransitIds.delete(cardId);
		}
	}

	/** Returns the live FlightPose for a card id if registered. */
	getPose(cardId: string): FlightPose | undefined {
		return this.#poses.get(cardId);
	}

	/** Applies this card's registered pose-provider's result directly (no
	 *  tween) — called by an owner's own reactive layout effect for every card
	 *  it owns, every time that layout recomputes, but only takes effect while
	 *  the card is idle; a GSAP-owned in-transit pose is left alone. */
	applyIdlePoseIfNotInTransit(cardId: string): void {
		untrack(() => {
			if (this.#inTransitIds.has(cardId)) return;
			const provider = this.#poseProviders.get(cardId);
			const pose = this.#poses.get(cardId);
			if (!provider || !pose) return;
			const [x, y, z] = provider();
			pose.x = x;
			pose.y = y;
			pose.z = z;
			pose.dragT = 0;
		});
	}

	/** A real card's entry never disappears once created — its identity/meta
	 *  survives forever unless the card genuinely leaves the game (removeEntry).
	 *  Idempotent: seeds a pose only if one doesn't already exist. */
	ensureEntry(cardId: string, initialPose: FlightPose, card: CardMeta | null): FlightPose {
		return untrack(() => {
			let pose = this.#poses.get(cardId);
			if (!pose) {
				const reactivePose = $state({ ...initialPose });
				pose = reactivePose;
				this.#poses.set(cardId, pose);
			}
			if (card) this.#cardMeta.set(cardId, card);
			let handle = this.#flightHandles.get(cardId);
			if (!handle) {
				const meta = this.#cardMeta.get(cardId) ?? { type: "wild", value: "0" };
				const dec = this.#decorations.get(cardId);
				const newHandle: FlightHandle = $state({ id: cardId, pose, card: meta, decoration: dec });
				handle = newHandle;
				this.#flightHandles.set(cardId, newHandle);
				this.activeFlights.push(newHandle);
			} else if (card) {
				handle.card = card;
			}
			return pose;
		});
	}

	/** LocalHand3D-owned visual extras (hover/drag/shadow/highlight) for a card
	 *  currently in its hand — read by AllCards3D's single render site. See
	 *  the shared render site owns mounting CardMesh3D,
	 *  owners only ever compute layout + these decorations, never mount it
	 *  themselves. */
	setDecoration(cardId: string, decoration: CardDecoration | undefined): void {
		untrack(() => {
			const merged = decoration ? { ...this.#decorations.get(cardId), ...decoration } : undefined;
			this.#decorations.set(cardId, merged);
			const handle = this.#flightHandles.get(cardId);
			if (handle) {
				handle.decoration = merged;
			}
		});
	}

	/** Outright deletes decoration for a card, preventing stale hover/drag
	 *  decorations from merging into flights. */
	clearDecoration(cardId: string): void {
		untrack(() => {
			this.#decorations.delete(cardId);
			const handle = this.#flightHandles.get(cardId);
			if (handle) {
				handle.decoration = undefined;
			}
		});
	}

	/** A card genuinely leaving the game for good (never happens for Uno's own
	 *  cards mid-match, but kept for symmetry/cleanup, e.g. on disconnect). */
	removeEntry(cardId: string): void {
		this.#inTransitIds.delete(cardId);
		this.#poseProviders.delete(cardId);
		this.#decorations.delete(cardId);
		this.#retireFlight(cardId);
	}

	/** Drives per-frame pose composition (hover lift, pushX lerp, rotation punch)
	 *  for all active entries in the registry. */
	tick(deltaS: number): void {
		untrack(() => {
			for (const [cardId, pose] of this.#poses) {
				const decoration = this.#decorations.get(cardId);
				const isHovered = Boolean(decoration?.hovered);
				const wasHovered = Boolean(this.#prevHoveredMap.get(cardId));

				if (pose.liftT === undefined) pose.liftT = 0;
				if (pose.pushX === undefined) pose.pushX = 0;
				if (pose.hoverSpinDeg === undefined) pose.hoverSpinDeg = 0;

				if (isHovered !== wasHovered) {
					this.#prevHoveredMap.set(cardId, isHovered);
					if (isHovered) {
						if (decoration?.instant) {
							this.#liftTweens.get(cardId)?.kill();
							this.#liftTweens.delete(cardId);
							pose.liftT = 1;
						} else {
							this.#liftTweens.get(cardId)?.kill();
							const tween = gsap.to(pose, {
								liftT: 1,
								duration: 0.15,
								ease: "back.out",
								onComplete: () => {
									if (this.#liftTweens.get(cardId) === tween) {
										this.#liftTweens.delete(cardId);
									}
								}
							});
							this.#liftTweens.set(cardId, tween);
							this.triggerPunch(cardId, 5);
						}
					} else {
						if (decoration?.instant) {
							this.#liftTweens.get(cardId)?.kill();
							this.#liftTweens.delete(cardId);
							pose.liftT = 0;
							this.#punchTimelines.get(cardId)?.kill();
							this.#punchTimelines.delete(cardId);
							pose.hoverSpinDeg = 0;
						} else {
							this.#liftTweens.get(cardId)?.kill();
							const tween = gsap.to(pose, {
								liftT: 0,
								duration: 0.15,
								ease: "power2.out",
								onComplete: () => {
									if (this.#liftTweens.get(cardId) === tween) {
										this.#liftTweens.delete(cardId);
									}
								}
							});
							this.#liftTweens.set(cardId, tween);
						}
					}
				} else if (decoration?.instant) {
					this.#liftTweens.get(cardId)?.kill();
					this.#liftTweens.delete(cardId);
					pose.liftT = isHovered ? 1 : 0;
				}

				const targetPushX = decoration?.pushX ?? 0;
				const currentPushX = pose.pushX ?? 0;
				if (decoration?.instant) {
					pose.pushX = targetPushX;
				} else if (currentPushX !== targetPushX) {
					const t = Math.min(1, deltaS <= 0 ? 0 : deltaS / 0.1);
					const nextPushX = currentPushX + (targetPushX - currentPushX) * t;
					pose.pushX = Math.abs(targetPushX - nextPushX) < 1e-4 ? targetPushX : nextPushX;
				}
			}
		});
	}

	/** Triggers a two-leg overshoot and settle rotation punch on pose.hoverSpinDeg. */
	triggerPunch(cardId: string, angleDeg: number = 5): void {
		untrack(() => {
			const pose = this.#poses.get(cardId);
			if (!pose) return;
			if (pose.hoverSpinDeg === undefined) pose.hoverSpinDeg = 0;
			this.#punchTimelines.get(cardId)?.kill();
			const tl = gsap.timeline();
			tl.to(pose, {
				hoverSpinDeg: angleDeg,
				duration: 0.075,
				ease: "power2.out"
			});
			tl.to(pose, {
				hoverSpinDeg: 0,
				duration: 0.075,
				ease: "elastic.out"
			});
			tl.eventCallback("onComplete", () => {
				if (this.#punchTimelines.get(cardId) === tl) {
					this.#punchTimelines.delete(cardId);
				}
			});
			this.#punchTimelines.set(cardId, tl);
		});
	}

	/** Queues a batch of beats. `resolveAnchor` resolves every named world
	 *  position this batch's steps may reference. Returns a promise resolving
	 *  once every beat in the batch has completed (or been skipped) —
	 *  callers that need "wait for this to finish" (e.g. a reshuffle before
	 *  the next turn) await it directly. */
	enqueue(
		beats: AnimationBeat[],
		resolveAnchor: (name: string) => [number, number, number]
	): Promise<void> {
		return new Promise((resolve) => {
			// Reserve every target up front, not just when its beat starts
			// playing in #playBatch. A beat queued behind a currently-playing
			// one can wait many frames, and during that window an owner's
			// cleanup effect (LocalHand3D/DiscardPile3D seeing the card leave
			// its row, not yet "in transit", not yet in the discard) would
			// removeEntry it — deleting the pose AND meta the flight was seeded
			// with. The flight then recreated the card at the world origin with
			// CardRegistry's dummy {wild,"0"} meta: the "white zero in the
			// middle of the screen" the spectator saw. Marking them in-transit
			// here makes owners leave the seeded pose alone until its beat runs.
			for (const beat of beats) {
				for (const step of beat) this.#inTransitIds.add(step.target);
			}
			this.#pending.push({ beats, resolveAnchor, resolve });
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
		for (const step of beat) this.#inTransitIds.add(step.target);
		try {
			const ctx: RenderContext = {
				getPose: (cardId, startPose) => {
					let pose = this.#poses.get(cardId);
					if (!pose) {
						const reactivePose = $state({ ...startPose });
						pose = reactivePose;
						this.#poses.set(cardId, pose);
					}
					let handle = this.#flightHandles.get(cardId);
					if (!handle) {
						const meta = this.#cardMeta.get(cardId) ?? { type: "wild", value: "0" };
						const dec = this.#decorations.get(cardId);
						const newHandle: FlightHandle = $state({
							id: cardId,
							pose,
							card: meta,
							decoration: dec
						});
						this.#flightHandles.set(cardId, newHandle);
						this.activeFlights.push(newHandle);
					}
					return pose;
				},
				resolveAnchor: batch.resolveAnchor
			};

			const timeline = gsap.timeline();
			for (const step of beat) {
				const renderer = this.#registry[step.op];
				if (!renderer) {
					console.warn(
						`CardRegistry: unknown op "${step.op}" for target "${step.target}" — skipping.`
					);
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
					if (typeof step.payload?._onCompleteSafe === "function") {
						try {
							step.payload._onCompleteSafe();
						} catch (e) {
							console.error(e);
						}
					}
					const usedLater = batch.beats
						.slice(beatIndex + 1)
						.some((laterBeat) => laterBeat.some((laterStep) => laterStep.target === step.target));
					// A card can also be needed by a batch enqueued separately (e.g.
					// checkLocalKeptDrawn's move-to-hand batch queued right after a
					// flip batch for the same drawn card) — that batch sits in
					// #pending behind this one, not inside `batch.beats`. Missing
					// this let finishBeat retire the pose/meta the queued batch was
					// about to animate, producing a pop/teleport when it finally ran.
					const usedInLaterPending = this.#pending
						.slice(1)
						.some((pendingBatch) =>
							pendingBatch.beats.some((laterBeat) =>
								laterBeat.some((laterStep) => laterStep.target === step.target)
							)
						);
					if (usedLater || usedInLaterPending) continue;
					this.#inTransitIds.delete(step.target);
					const provider = this.#poseProviders.get(step.target);
					if (provider) {
						// Idle-at-current-owner's-layout, not deleted: this is what
						// makes the flight-to-idle handoff a no-op instead of the
						// 1-frame pop the old hide/show + delete model produced.
						this.applyIdlePoseIfNotInTransit(step.target);
					} else {
						this.#retireFlight(step.target);
					}
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
			console.error(
				`CardRegistry: beat ${beatIndex} failed unexpectedly — advancing past it.`,
				err
			);
			// Release the reservation enqueue() made for this beat's targets, or
			// a card whose renderer threw stays permanently in-transit and its
			// owner can never re-sync it. Targets a later beat still needs stay
			// reserved.
			const failedBeat = batch.beats[beatIndex];
			for (const step of failedBeat) {
				const usedLater = batch.beats
					.slice(beatIndex + 1)
					.some((laterBeat) => laterBeat.some((laterStep) => laterStep.target === step.target));
				if (!usedLater) this.#inTransitIds.delete(step.target);
			}
			this.#currentTimeline = null;
			this.#finishCurrentBeat = null;
			this.#playBatch(batch, beatIndex + 1);
		}
	}

	#retireFlight(cardId: string): void {
		this.#poses.delete(cardId);
		this.#cardMeta.delete(cardId);
		this.#flightHandles.delete(cardId);
		this.#prevHoveredMap.delete(cardId);
		this.#liftTweens.get(cardId)?.kill();
		this.#liftTweens.delete(cardId);
		this.#punchTimelines.get(cardId)?.kill();
		this.#punchTimelines.delete(cardId);
		const idx = this.activeFlights.findIndex((f) => f.id === cardId);
		if (idx !== -1) {
			this.activeFlights.splice(idx, 1);
		}
	}
}

export const CARD_REGISTRY_KEY = Symbol("card-registry");

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
		if (!(err instanceof Error) || !err.message.includes("lifecycle_outside_component")) {
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
