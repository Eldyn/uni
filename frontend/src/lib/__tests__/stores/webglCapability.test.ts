import { describe, it, expect } from "vitest";
import { storeWebglCapability } from "$stores/webglCapability.svelte";

describe("storeWebglCapability deviceTier", () => {
	it("returns high when hardwareAccelerated is true and reducedMotion is false", () => {
		storeWebglCapability.hardwareAccelerated = true;
		storeWebglCapability.reducedMotion = false;
		expect(storeWebglCapability.deviceTier).toBe("high");
	});

	it("returns low when reducedMotion is true or hardwareAccelerated is false", () => {
		storeWebglCapability.hardwareAccelerated = true;
		storeWebglCapability.reducedMotion = true;
		expect(storeWebglCapability.deviceTier).toBe("low");

		storeWebglCapability.hardwareAccelerated = false;
		storeWebglCapability.reducedMotion = false;
		expect(storeWebglCapability.deviceTier).toBe("low");
	});
});
