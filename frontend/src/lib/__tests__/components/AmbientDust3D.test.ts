// AmbientDust3D's per-frame task is driven directly with a controlled clock
// (a mocked performance.now, since useTask: vi.fn() never auto-invokes it).
// The user-reported bug was that dust advanced only on the 12fps step grid:
// uTime was snapped to the step and invalidate() was skipped mid-step, so the
// whole screen read as laggy. These tests pin the continuous contract — uTime
// (and uRippleElapsed) move with the raw frame clock, and every frame
// invalidates — against a real three ShaderMaterial observed through the
// component's Threlte stand-in, without a GL context.

import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import MockTintMaterial from "./MockTintMaterial.svelte";
import { meshInstances, resetMockState } from "./drawPileMockState";

const { invalidateSpy } = vi.hoisted(() => ({ invalidateSpy: vi.fn() }));

vi.mock("@threlte/core", () => ({
	T: new Proxy({}, { get: () => MockTintMaterial }),
	useTask: vi.fn(),
	useThrelte: () => ({ invalidate: invalidateSpy })
}));

vi.mock("$components/game/three/textures", () => ({
	loadTexture: vi.fn().mockResolvedValue({})
}));

import { render, cleanup } from "@testing-library/svelte";
import { useTask } from "@threlte/core";
import type { ShaderMaterial } from "three";
import AmbientDust3D from "$components/game/three/AmbientDust3D.svelte";
import { storeMatRipple } from "$components/game/three/ripple/matRipple.svelte";
import type { MatPlacement } from "$components/game/layout/playmat";
import type { ViewportInfo } from "$components/game/layout/seatLayout";

const mat: MatPlacement = {
	size: [10, 10],
	offsetX: 0,
	offsetZ: 0,
	quarterTurn: false,
	bounds: { left: -5, right: 5, far: -5, near: 5 }
};

const viewport: ViewportInfo = { width: 1920, height: 1080, orientation: "landscape" };

async function flush() {
	await new Promise((r) => setTimeout(r, 10));
}

function latestTaskCallback(): () => void {
	const calls = vi.mocked(useTask).mock.calls;
	return calls[calls.length - 1][0] as unknown as () => void;
}

/** The live ShaderMaterial the component handed to its instanced mesh. */
function dustMaterial(): ShaderMaterial {
	const mesh = meshInstances.find((m) => Array.isArray(m.args));
	return (mesh!.args as unknown[])[1] as ShaderMaterial;
}

describe("AmbientDust3D continuous clock", () => {
	beforeEach(() => {
		resetMockState();
		vi.mocked(useTask).mockClear();
		invalidateSpy.mockClear();
	});

	afterEach(() => {
		cleanup();
		vi.restoreAllMocks();
		storeMatRipple.active = false;
		storeMatRipple.strength = "normal";
	});

	it("advances uTime from the raw frame clock and invalidates on every frame, not per 12fps step", async () => {
		const nowSpy = vi.spyOn(performance, "now");
		nowSpy.mockReturnValue(1000);

		render(AmbientDust3D, { props: { mat, viewport } });
		await flush();
		const material = dustMaterial();
		const step = latestTaskCallback();

		// Two frames inside the SAME 12fps bucket (one step ≈ 83.3ms): the old
		// step-gated code skipped the second invalidate and froze uTime on the
		// bucket boundary.
		invalidateSpy.mockClear();
		nowSpy.mockReturnValue(1000);
		step();
		expect(material.uniforms.uTime.value).toBeCloseTo(1.0, 6);
		expect(invalidateSpy).toHaveBeenCalledTimes(1);

		nowSpy.mockReturnValue(1010);
		step();
		expect(material.uniforms.uTime.value).toBeCloseTo(1.01, 6);
		expect(invalidateSpy).toHaveBeenCalledTimes(2);
	});

	it("feeds the puff a continuous uRippleElapsed, not a 12fps-quantized one", async () => {
		const nowSpy = vi.spyOn(performance, "now");
		nowSpy.mockReturnValue(1000);

		render(AmbientDust3D, { props: { mat, viewport } });
		await flush();
		const material = dustMaterial();
		const step = latestTaskCallback();

		const start = 5000;
		storeMatRipple.active = true;
		storeMatRipple.strength = "wild";
		storeMatRipple.originUv = { u: 0.5, v: 0.5 };
		storeMatRipple.startTimeMs = start;
		storeMatRipple.durationMs = 480;
		storeMatRipple.maxRadius = 1;

		// 40ms into the sweep: still within the first 83ms step, but the puff
		// clock must already read 0.04 — continuous, not snapped to step 0.
		nowSpy.mockReturnValue(start + 40);
		step();
		expect(material.uniforms.uRippleElapsed.value).toBeCloseTo(0.04, 6);
		expect(material.uniforms.uRippleWild.value).toBe(1);

		nowSpy.mockReturnValue(start + 50);
		step();
		expect(material.uniforms.uRippleElapsed.value).toBeCloseTo(0.05, 6);
	});

	it("retains the wild puff snapshot after the ripple ends so the held impulse never snaps back", async () => {
		const nowSpy = vi.spyOn(performance, "now");
		nowSpy.mockReturnValue(1000);

		render(AmbientDust3D, { props: { mat, viewport } });
		await flush();
		const material = dustMaterial();
		const step = latestTaskCallback();

		storeMatRipple.active = true;
		storeMatRipple.strength = "wild";
		storeMatRipple.originUv = { u: 0.5, v: 0.5 };
		storeMatRipple.startTimeMs = 5000;
		storeMatRipple.durationMs = 480;
		storeMatRipple.maxRadius = 1;

		nowSpy.mockReturnValue(5480);
		step();
		expect(material.uniforms.uRippleWild.value).toBe(1);

		// The mat ripple commits and clears, but the dust puff must hold: a
		// snapshot dropped here would snap every displaced mote back to its
		// undisplaced position.
		storeMatRipple.active = false;
		nowSpy.mockReturnValue(5600);
		step();
		expect(material.uniforms.uRippleWild.value).toBe(1);
		expect(material.uniforms.uRippleElapsed.value).toBeCloseTo(0.6, 6);

		// Far past the old 900ms retention window (expiresAtMs = 6380): the
		// snapshot must still be live for the whole match.
		nowSpy.mockReturnValue(60000);
		step();
		expect(material.uniforms.uRippleWild.value).toBe(1);
		expect(material.uniforms.uRippleElapsed.value).toBeCloseTo(55, 6);
	});

	it("does not snapshot a normal ripple, and drops the snapshot on unmount", async () => {
		const nowSpy = vi.spyOn(performance, "now");
		nowSpy.mockReturnValue(1000);

		render(AmbientDust3D, { props: { mat, viewport } });
		await flush();
		let material = dustMaterial();
		let step = latestTaskCallback();

		// A normal ripple must not trigger the wild puff at all.
		storeMatRipple.active = true;
		storeMatRipple.strength = "normal";
		storeMatRipple.originUv = { u: 0.5, v: 0.5 };
		storeMatRipple.startTimeMs = 5000;
		storeMatRipple.durationMs = 480;
		storeMatRipple.maxRadius = 1;
		nowSpy.mockReturnValue(5100);
		step();
		expect(material.uniforms.uRippleWild.value).toBe(0);

		// A wild ripple then takes, proving the snapshot path is live...
		storeMatRipple.strength = "wild";
		step();
		expect(material.uniforms.uRippleWild.value).toBe(1);

		// ...and a fresh mount (a new match) starts with no carried snapshot.
		storeMatRipple.active = false;
		cleanup();
		resetMockState();
		nowSpy.mockReturnValue(70000);
		render(AmbientDust3D, { props: { mat, viewport } });
		await flush();
		material = dustMaterial();
		step = latestTaskCallback();
		step();
		expect(material.uniforms.uRippleWild.value).toBe(0);
	});
});
