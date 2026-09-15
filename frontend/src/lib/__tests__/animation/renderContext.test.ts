import { describe, it, expect } from "vitest";
import { createDefaultFlightPose } from "$components/game/animation/renderContext";

describe("FlightPose", () => {
	it("includes liftT, pushX, and hoverSpinDeg default 0", () => {
		const pose = createDefaultFlightPose();
		expect(pose.liftT).toBe(0);
		expect(pose.pushX).toBe(0);
		expect(pose.hoverSpinDeg).toBe(0);
	});

	it("returns a complete default FlightPose object", () => {
		const pose = createDefaultFlightPose();
		expect(pose).toEqual({
			x: 0,
			y: 0,
			z: 0,
			spinDeg: 0,
			flipDeg: 0,
			scale: 1,
			turned: false,
			opacity: 1,
			liftT: 0,
			pushX: 0,
			hoverSpinDeg: 0
		});
	});

	it("accepts partial overrides", () => {
		const pose = createDefaultFlightPose({ x: 10, liftT: 0.5, turned: true });
		expect(pose.x).toBe(10);
		expect(pose.y).toBe(0);
		expect(pose.liftT).toBe(0.5);
		expect(pose.pushX).toBe(0);
		expect(pose.turned).toBe(true);
	});
});
