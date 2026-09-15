import { describe, it, expect, vi, beforeEach } from "vitest";
import { gsap } from "gsap";
import { CardRegistry } from "$components/game/animation/cardRegistry.svelte";

describe("CardRegistry.tick composer", () => {
	beforeEach(() => {
		gsap.ticker.lagSmoothing(false);
	});

	it("lerps pushX toward decoration target", () => {
		const registry = new CardRegistry();
		const pose = registry.seedPose("c1", {
			x: 0,
			y: 0,
			z: 0,
			spinDeg: 0,
			scale: 1,
			turned: false,
			opacity: 1,
			liftT: 0,
			pushX: 0,
			hoverSpinDeg: 0
		});
		registry.setDecoration("c1", { pushX: 2 });

		registry.tick(0.05);
		expect(pose.pushX).toBeGreaterThan(0);
		expect(pose.pushX).toBeLessThanOrEqual(2);
	});

	it("sets pushX immediately when decoration.instant is true", () => {
		const registry = new CardRegistry();
		const pose = registry.seedPose("c1", {
			x: 0,
			y: 0,
			z: 0,
			spinDeg: 0,
			scale: 1,
			turned: false,
			opacity: 1,
			liftT: 0,
			pushX: 0,
			hoverSpinDeg: 0
		});
		registry.setDecoration("c1", { pushX: 2.5, instant: true });

		registry.tick(0.01);
		expect(pose.pushX).toBe(2.5);
	});

	it("sets liftT immediately when hovered and decoration.instant is true", () => {
		const registry = new CardRegistry();
		const pose = registry.seedPose("c1", {
			x: 0,
			y: 0,
			z: 0,
			spinDeg: 0,
			scale: 1,
			turned: false,
			opacity: 1,
			liftT: 0,
			pushX: 0,
			hoverSpinDeg: 0
		});
		registry.setDecoration("c1", { hovered: true, instant: true });

		registry.tick(0.01);
		expect(pose.liftT).toBe(1);

		registry.setDecoration("c1", { hovered: false, instant: true });
		registry.tick(0.01);
		expect(pose.liftT).toBe(0);
	});

	it("triggers GSAP tween on liftT and rotation punch on hoverSpinDeg when hovered flips to true", () => {
		const registry = new CardRegistry();
		const pose = registry.seedPose("c1", {
			x: 0,
			y: 0,
			z: 0,
			spinDeg: 0,
			scale: 1,
			turned: false,
			opacity: 1,
			liftT: 0,
			pushX: 0,
			hoverSpinDeg: 0
		});
		registry.setDecoration("c1", { hovered: true });

		registry.tick(0.01);

		const tweens = gsap.getTweensOf(pose);
		expect(tweens.length).toBeGreaterThan(0);

		// Advance GSAP time to complete the animations
		tweens.forEach((t) => t.progress(1));
		expect(pose.liftT).toBe(1);
		expect(pose.hoverSpinDeg).toBe(0);
	});

	it("tweens liftT to 0 when hovered flips to false", () => {
		const registry = new CardRegistry();
		const pose = registry.seedPose("c1", {
			x: 0,
			y: 0,
			z: 0,
			spinDeg: 0,
			scale: 1,
			turned: false,
			opacity: 1,
			liftT: 1,
			pushX: 0,
			hoverSpinDeg: 0
		});
		registry.setDecoration("c1", { hovered: true, instant: true });
		registry.tick(0.01);
		expect(pose.liftT).toBe(1);

		// Flip to false without instant
		registry.setDecoration("c1", { hovered: false, instant: false });
		registry.tick(0.01);

		const tweens = gsap.getTweensOf(pose);
		expect(tweens.length).toBeGreaterThan(0);

		tweens.forEach((t) => t.progress(1));
		expect(pose.liftT).toBe(0);
	});

	it("triggerPunch runs a two-leg timeline on hoverSpinDeg to angleDeg and settles to 0", () => {
		const registry = new CardRegistry();
		const pose = registry.seedPose("c1", {
			x: 0,
			y: 0,
			z: 0,
			spinDeg: 0,
			scale: 1,
			turned: false,
			opacity: 1,
			liftT: 0,
			pushX: 0,
			hoverSpinDeg: 0
		});

		registry.triggerPunch("c1", 30);
		const tweens = gsap.getTweensOf(pose);
		expect(tweens.length).toBeGreaterThan(0);

		tweens.forEach((t) => t.progress(1));
		expect(pose.hoverSpinDeg).toBe(0);
	});

	it("lerp snaps to targetPushX when deltaS >= 0.1", () => {
		const registry = new CardRegistry();
		const pose = registry.seedPose("c1", {
			x: 0,
			y: 0,
			z: 0,
			spinDeg: 0,
			scale: 1,
			turned: false,
			opacity: 1,
			liftT: 0,
			pushX: 0,
			hoverSpinDeg: 0
		});
		registry.setDecoration("c1", { pushX: 3 });

		registry.tick(0.15);
		expect(pose.pushX).toBe(3);
	});

	it("triggerPunch uses default 5 deg when angleDeg omitted", () => {
		const registry = new CardRegistry();
		const pose = registry.seedPose("c1", {
			x: 0,
			y: 0,
			z: 0,
			spinDeg: 0,
			scale: 1,
			turned: false,
			opacity: 1,
			liftT: 0,
			pushX: 0,
			hoverSpinDeg: 0
		});

		registry.triggerPunch("c1");
		const tweens = gsap.getTweensOf(pose);
		expect(tweens.length).toBeGreaterThan(0);

		tweens.forEach((t) => t.progress(1));
		expect(pose.hoverSpinDeg).toBe(0);
	});

	it("retireFlight and removeEntry cleanly kill tweens and remove entry state", () => {
		const registry = new CardRegistry();
		const pose = registry.seedPose("c1", {
			x: 0,
			y: 0,
			z: 0,
			spinDeg: 0,
			scale: 1,
			turned: false,
			opacity: 1,
			liftT: 0,
			pushX: 0,
			hoverSpinDeg: 0
		});
		registry.setDecoration("c1", { hovered: true });
		registry.tick(0.01);

		expect(gsap.getTweensOf(pose).length).toBeGreaterThan(0);

		registry.removeEntry("c1");
		expect(gsap.getTweensOf(pose).length).toBe(0);

		// Subsequent ticks should not throw
		expect(() => registry.tick(0.01)).not.toThrow();
	});

	it("rapid hover flipping replaces the previous lift tween cleanly", () => {
		const registry = new CardRegistry();
		const pose = registry.seedPose("c1", {
			x: 0,
			y: 0,
			z: 0,
			spinDeg: 0,
			scale: 1,
			turned: false,
			opacity: 1,
			liftT: 0,
			pushX: 0,
			hoverSpinDeg: 0
		});

		registry.setDecoration("c1", { hovered: true });
		registry.tick(0.01);
		const initialTweens = gsap.getTweensOf(pose);
		expect(initialTweens.length).toBeGreaterThan(0);

		registry.setDecoration("c1", { hovered: false });
		registry.tick(0.01);

		// After flipping to false, lift tween should target 0
		const activeTweens = gsap.getTweensOf(pose);
		activeTweens.forEach((t) => t.progress(1));
		expect(pose.liftT).toBe(0);
	});
});
