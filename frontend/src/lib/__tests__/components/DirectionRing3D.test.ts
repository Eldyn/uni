import { describe, it, expect, vi, afterEach, beforeEach } from "vitest";
import { flushSync } from "svelte";
import MockThrelte from "./MockThrelte.svelte";
import MockThrelteMesh from "./MockThrelteMesh.svelte";
import MockThrelteGroup from "./MockThrelteGroup.svelte";

vi.mock("@threlte/core", () => ({
	T: new Proxy(
		{},
		{
			get: (_, key) =>
				key === "Mesh" ? MockThrelteMesh : key === "Group" ? MockThrelteGroup : MockThrelte
		}
	),
	useTask: vi.fn(),
	useThrelte: () => ({ invalidate: vi.fn() })
}));

vi.mock("$components/game/three/directionRingTexture", () => ({
	directionRingTexture: vi.fn(() => ({}))
}));

import { render, cleanup, screen } from "@testing-library/svelte";
import { gsap } from "gsap";
import DirectionRing3D from "$components/game/three/DirectionRing3D.svelte";
import { storeAnimation } from "$stores/animation.svelte";
import { storeDirectionRing } from "$stores/directionRing.svelte";

const mat = { size: [16, 9], offsetX: 0, offsetZ: 0, quarterTurn: false } as never;

describe("DirectionRing3D", () => {
	beforeEach(() => {
		storeDirectionRing.reset();
		storeAnimation.enabled = true;
	});

	afterEach(() => {
		cleanup();
		vi.restoreAllMocks();
	});

	it("renders the ring, also under reduced motion", () => {
		storeAnimation.enabled = false;
		render(DirectionRing3D, { props: { mat } });
		flushSync();
		expect(screen.getByTestId("direction-ring")).toBeTruthy();
	});

	it("follows the mat's offset and quarter turn like the felt", () => {
		const offsetMat = { size: [16, 9], offsetX: -2.5, offsetZ: 1.25, quarterTurn: true } as never;
		render(DirectionRing3D, { props: { mat: offsetMat } });
		flushSync();
		const frame = screen.getByTestId("direction-ring-frame");
		expect(frame.dataset.x).toBe("-2.5");
		expect(frame.dataset.z).toBe("1.25");
		expect(Number(frame.dataset.rotationY)).toBeCloseTo(Math.PI / 2);
		expect(frame.contains(screen.getByTestId("direction-ring"))).toBe(true);
	});

	it("does not tween under reduced motion", () => {
		storeAnimation.enabled = false;
		const tweenSpy = vi.spyOn(gsap, "to");
		render(DirectionRing3D, { props: { mat } });
		storeDirectionRing.reverseTo(-1);
		flushSync();
		expect(tweenSpy).not.toHaveBeenCalled();
	});

	it("flips once for a reverse", () => {
		const tweenSpy = vi.spyOn(gsap, "to");
		render(DirectionRing3D, { props: { mat } });
		storeDirectionRing.reverseTo(-1);
		flushSync();
		expect(tweenSpy).toHaveBeenCalledTimes(1);
	});

	it("queues a second flip behind the first when two reverses land together", () => {
		const tweenSpy = vi.spyOn(gsap, "to");
		render(DirectionRing3D, { props: { mat } });
		storeDirectionRing.reverseTo(-1);
		storeDirectionRing.reverseTo(1);
		flushSync();
		expect(tweenSpy).toHaveBeenCalledTimes(1);
		const firstTween = tweenSpy.mock.results[0].value as gsap.core.Tween;
		firstTween.progress(1);
		expect(tweenSpy).toHaveBeenCalledTimes(2);
	});

	it("snaps to the current direction on tab return without replaying flips", () => {
		const tweenSpy = vi.spyOn(gsap, "to");
		render(DirectionRing3D, { props: { mat } });
		// INFO: while the tab is hidden GSAP does not tick, so the first flip
		// sits at progress 0 and the rest wait in the queue.
		storeDirectionRing.reverseTo(-1);
		flushSync();
		storeDirectionRing.reverseTo(1);
		flushSync();
		storeDirectionRing.reverseTo(-1);
		flushSync();
		const hiddenTweens = tweenSpy.mock.results.map((r) => r.value as gsap.core.Tween);

		storeDirectionRing.settle();
		flushSync();

		expect(screen.getByTestId("direction-ring").dataset.scaleX).toBe("-1");
		expect(hiddenTweens.length).toBeGreaterThan(0);
		for (const tween of hiddenTweens) expect(gsap.getTweensOf(tween.targets())).toHaveLength(0);
		expect(tweenSpy).toHaveBeenCalledTimes(hiddenTweens.length);
	});
});
