import { gsap } from "gsap";
import {
	INHERIT_EASE,
	SPIN_EASE,
	spinDurations,
	spinStepsBetween
} from "$components/game/layout/tableSpin";
import { storeAnimation } from "./animation.svelte";

export type SpinPhase = "idle" | "spin" | "inherit";

export interface HandMorph {
	/** Player the morph belongs to. */
	username: string;
	/** World pose per card index: arc source for "in", arc target for "out". */
	poses: [number, number, number][];
	/** Whether the morphed cards end face-up. */
	open: boolean;
}

export interface SpinTransition {
	order: readonly string[];
	from: string;
	to: string;
	steps: number;
	spinAngle: number;
	incoming: HandMorph | null;
	outgoing: HandMorph | null;
}

class StoreTableSpin {
	renderPov = $state<string | null>(null);
	targetPov = $state<string | null>(null);
	phase = $state<SpinPhase>("idle");
	spinProgress = $state(0);
	inheritProgress = $state(0);
	pileAngle = $state(0);
	transition = $state<SpinTransition | null>(null);

	#spinTl: gsap.core.Timeline | null = null;
	#inheritTl: gsap.core.Timeline | null = null;
	#lastDirection: 1 | -1 = 1;
	#seeded = false;

	get active(): boolean {
		return this.phase !== "idle";
	}

	/** Snap the whole transition to its end and commit. */
	skip(): void {
		if (this.phase === "idle") return;
		this.#kill();
		const t = this.transition;
		if (t) this.pileAngle += t.spinAngle;
		if (t) this.renderPov = t.to;
		this.spinProgress = 1;
		this.inheritProgress = 1;
		this.phase = "idle";
		this.transition = null;
	}

	reset(): void {
		this.#kill();
		this.renderPov = null;
		this.targetPov = null;
		this.phase = "idle";
		this.spinProgress = 0;
		this.inheritProgress = 0;
		this.pileAngle = 0;
		this.transition = null;
		this.#seeded = false;
	}

	/** Roster/order/viewport change or eliminated target: settle instantly. */
	cancelAndCommit(): void {
		this.#kill();
		if (this.targetPov !== null) this.renderPov = this.targetPov;
		this.phase = "idle";
		this.spinProgress = 0;
		this.inheritProgress = 0;
		this.transition = null;
	}

	/**
	 * GameBoard calls this whenever the resolved POV target changes. `order`
	 * is the ring in slot order (slot 0 = the outgoing POV) and `spinAngle`
	 * is the signed sweep of the incoming seat to the bottom pivot.
	 */
	syncTarget(
		target: string | null,
		order: readonly string[],
		spinAngle: number,
		incoming: HandMorph | null,
		outgoing: HandMorph | null
	): void {
		if (!this.#seeded) {
			this.#seeded = true;
			this.targetPov = target;
			this.renderPov = target;
			return;
		}
		// A re-run caused by our own renderPov commit must not cancel the
		// in-flight phase it just started.
		if (target === this.targetPov && this.phase !== "idle") return;
		this.targetPov = target;
		if (this.phase !== "idle") this.cancelAndCommit();
		if (target === this.renderPov) return;
		if (target === null || order.length <= 1) {
			this.renderPov = target;
			return;
		}
		const from = this.renderPov ?? order[0];
		const steps = spinStepsBetween(order, from, target, this.#lastDirection);
		if (steps === 0) {
			this.renderPov = target;
			return;
		}
		this.#lastDirection = steps > 0 ? 1 : -1;
		this.transition = { order, from, to: target, steps, spinAngle, incoming, outgoing };

		const { spin, inherit } = spinDurations(storeAnimation.speedMultiplier, storeAnimation.enabled);
		if (spin === 0) {
			this.pileAngle += spinAngle;
			this.renderPov = target;
			this.inheritProgress = 1;
			this.transition = null;
			return;
		}
		this.#startSpin(spin, inherit);
	}

	#startSpin(spin: number, inherit: number): void {
		this.phase = "spin";
		this.spinProgress = 0;
		const proxy = { t: 0 };
		this.#spinTl = gsap.timeline({ onComplete: () => this.#commitSpin(inherit) });
		this.#spinTl.to(proxy, {
			t: 1,
			duration: spin,
			ease: SPIN_EASE,
			onUpdate: () => {
				this.spinProgress = proxy.t;
			}
		});
	}

	#commitSpin(inherit: number): void {
		const t = this.transition;
		if (t) this.pileAngle += t.spinAngle;
		if (t) this.renderPov = t.to;
		this.#spinTl = null;
		this.#startInherit(inherit);
	}

	#startInherit(inherit: number): void {
		this.phase = "inherit";
		this.inheritProgress = 0;
		const proxy = { t: 0 };
		this.#inheritTl = gsap.timeline({ onComplete: () => this.#commitInherit() });
		this.#inheritTl.to(proxy, {
			t: 1,
			duration: inherit,
			ease: INHERIT_EASE,
			onUpdate: () => {
				this.inheritProgress = proxy.t;
			}
		});
	}

	#commitInherit(): void {
		this.phase = "idle";
		this.inheritProgress = 1;
		this.transition = null;
		this.#inheritTl = null;
	}

	#kill(): void {
		this.#spinTl?.kill();
		this.#inheritTl?.kill();
		this.#spinTl = null;
		this.#inheritTl = null;
	}
}

export const storeTableSpin = new StoreTableSpin();
