/**
 * @file damageFx.svelte.ts
 * @brief Coordinates screen vignette damage flash and knockback camera punch
 * when a player receives draw damage.
 */
import { gsap } from "gsap";
import { cameraPunchOffset } from "$components/game/layout/cameraRig";
import { storeCameraOffset } from "$stores/cameraOffset.svelte";
import { storeAnimation } from "$stores/animation.svelte";

class StoreDamageFx {
	/** 0 to 1 intensity of the red damage flash overlay on the screen vignette. */
	flashOpacity = $state(0);

	#flashTween: gsap.core.Tween | null = null;
	#punchTween: gsap.core.Timeline | null = null;

	triggerDamage(count: number): void {
		if (!storeAnimation.enabled || typeof window === "undefined") return;

		const speed = Math.max(0.1, storeAnimation.speedMultiplier);
		const intensity = Math.min(1, 0.4 + count * 0.15);
		const punchDistance = Math.min(0.45, 0.08 + count * 0.04);

		// Flash vignette
		this.#flashTween?.kill();
		this.flashOpacity = intensity;
		this.#flashTween = gsap.to(this, {
			flashOpacity: 0,
			duration: (0.35 + Math.min(0.35, count * 0.05)) / speed,
			ease: "power2.out",
			onComplete: () => {
				this.flashOpacity = 0;
				this.#flashTween = null;
			}
		});

		// Camera knockback punch
		this.#punchTween?.kill();
		const peak = cameraPunchOffset(punchDistance);
		const punch = { amount: 0 };
		const applyPunch = () => {
			storeCameraOffset.applyOffset({
				x: peak.x * punch.amount,
				y: peak.y * punch.amount,
				z: peak.z * punch.amount
			});
		};

		const attackS = 0.04 / speed;
		const releaseS = (0.12 + Math.min(0.18, count * 0.02)) / speed;

		this.#punchTween = gsap.timeline({
			onComplete: () => {
				storeCameraOffset.clearOffset();
				this.#punchTween = null;
			}
		});

		this.#punchTween
			.to(punch, {
				amount: 1,
				duration: attackS,
				ease: "power2.out",
				onUpdate: applyPunch
			})
			.to(punch, {
				amount: 0,
				duration: releaseS,
				ease: "power2.inOut",
				onUpdate: applyPunch
			});
	}

	reset(): void {
		this.#flashTween?.kill();
		this.#flashTween = null;
		this.#punchTween?.kill();
		this.#punchTween = null;
		this.flashOpacity = 0;
		storeCameraOffset.clearOffset();
	}
}

export const storeDamageFx = new StoreDamageFx();
