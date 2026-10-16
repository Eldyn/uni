import { describe, it, expect, beforeEach, afterEach, vi } from "vitest";
import { storeDamageFx } from "$stores/damageFx.svelte";
import { storeCameraOffset } from "$stores/cameraOffset.svelte";
import { storeAnimation } from "$stores/animation.svelte";

describe("storeDamageFx", () => {
	beforeEach(() => {
		storeAnimation.enabled = true;
		storeDamageFx.reset();
	});

	afterEach(() => {
		storeDamageFx.reset();
	});

	it("initializes with zero flashOpacity and clear camera offset", () => {
		expect(storeDamageFx.flashOpacity).toBe(0);
		expect(storeCameraOffset.offset).toEqual({ x: 0, y: 0, z: 0 });
	});

	it("triggers damage flash and camera offset punch", () => {
		storeDamageFx.triggerDamage(4);
		expect(storeDamageFx.flashOpacity).toBeGreaterThan(0);
		expect(storeDamageFx.flashOpacity).toBeLessThanOrEqual(1);
	});

	it("does not trigger when storeAnimation.enabled is false", () => {
		storeAnimation.enabled = false;
		storeDamageFx.triggerDamage(4);
		expect(storeDamageFx.flashOpacity).toBe(0);
		expect(storeCameraOffset.offset).toEqual({ x: 0, y: 0, z: 0 });
	});

	it("reset clears flashOpacity and camera offset", () => {
		storeDamageFx.triggerDamage(2);
		storeDamageFx.reset();
		expect(storeDamageFx.flashOpacity).toBe(0);
		expect(storeCameraOffset.offset).toEqual({ x: 0, y: 0, z: 0 });
	});
});
