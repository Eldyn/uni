/**
 * @file turnLoopMotion.ts
 * @brief Everything the play-direction loop does over time, with no
 * three.js and no Svelte: the pattern's march along the outline, the queue of
 * reverses and the brighten impulse each one plays.
 *
 * `phase`, `direction` and `tone` are plain `{ value }` objects meant to be
 * dropped straight into the loop shader's uniforms. Motion is gated by the
 * caller's `motionActive` (the mat-ripple gate): when it is false nothing
 * moves, a reverse flips the direction at once and the pose is static.
 */

import { gsap } from "gsap";
import type { DirectionSign } from "../animation/directionRing";
import {
	LOOP_FLIP_SECONDS,
	LOOP_IDLE_TONE,
	advancePhase,
	glowStepCount,
	glowTone
} from "../animation/loopPlan";

export interface TurnLoopMotionDeps {
	motionActive: () => boolean;
	speedMultiplier: () => number;
	onChange: () => void;
}

interface UniformNumber {
	value: number;
}

const MIN_SPEED_MULTIPLIER = 0.1;

export class TurnLoopMotion {
	readonly phase: UniformNumber = { value: 0 };
	readonly direction: UniformNumber = { value: 1 };
	readonly tone: UniformNumber = { value: LOOP_IDLE_TONE };
	loopLength = 1;

	#deps: TurnLoopMotionDeps;
	#queue: DirectionSign[] = [];
	#latest: DirectionSign = 1;
	#tween: gsap.core.Tween | null = null;

	constructor(deps: TurnLoopMotionDeps) {
		this.#deps = deps;
	}

	/** Queues the flips a reverse batch raised, oldest first. */
	enqueue(signs: DirectionSign[]): void {
		if (signs.length === 0) return;
		this.#latest = signs[signs.length - 1];
		if (!this.#deps.motionActive()) {
			this.snapTo(this.#latest);
			return;
		}
		this.#queue.push(...signs);
		this.#runNext();
	}

	/** Shows `sign` with no animation and drops queued flips (mount, new match,
	 *  tab return, desync). */
	snapTo(sign: DirectionSign): void {
		this.#stopTween();
		this.#queue = [];
		this.#latest = sign;
		this.direction.value = sign;
		this.tone.value = LOOP_IDLE_TONE;
		if (!this.#deps.motionActive()) this.phase.value = 0;
		this.#deps.onChange();
	}

	/** Call when the motion gate turns off: settle where the queue was heading. */
	motionStopped(): void {
		this.snapTo(this.#latest);
	}

	/** Real-frame step: the pattern crawls along its rendered direction. */
	tick(deltaSeconds: number): void {
		if (!this.#deps.motionActive()) return;
		this.phase.value = advancePhase(
			this.phase.value,
			deltaSeconds,
			this.direction.value,
			this.#deps.speedMultiplier(),
			this.loopLength
		);
		this.#deps.onChange();
	}

	dispose(): void {
		this.#stopTween();
		this.#queue = [];
	}

	#runNext(): void {
		if (this.#tween) return;
		const target = this.#queue.shift();
		if (target === undefined) return;

		// The chevrons flip the instant the reverse starts; the tween only
		// drives the brighten, stepped on the ambient 12fps grid.
		this.direction.value = target;
		const duration =
			LOOP_FLIP_SECONDS / Math.max(MIN_SPEED_MULTIPLIER, this.#deps.speedMultiplier());
		const steps = glowStepCount(duration);
		const progress = { value: 0 };
		this.#tween = gsap.to(progress, {
			value: 1,
			duration,
			ease: "power2.inOut",
			onUpdate: () => {
				const steppedProgress = Math.floor(progress.value * steps) / steps;
				this.tone.value = glowTone(Math.sin(Math.PI * steppedProgress));
				this.#deps.onChange();
			},
			onComplete: () => {
				this.#tween = null;
				this.tone.value = LOOP_IDLE_TONE;
				this.#deps.onChange();
				this.#runNext();
			}
		});
		this.#deps.onChange();
	}

	#stopTween(): void {
		this.#tween?.kill();
		this.#tween = null;
	}
}
