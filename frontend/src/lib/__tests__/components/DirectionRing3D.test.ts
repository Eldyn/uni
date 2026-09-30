import { describe, it, expect, vi, afterEach, beforeEach } from "vitest";
import { flushSync } from "svelte";
import MockThrelte from "./MockThrelte.svelte";
import MockThrelteMesh from "./MockThrelteMesh.svelte";

vi.mock("@threlte/core", () => ({
	T: new Proxy({}, { get: (_, key) => (key === "Mesh" ? MockThrelteMesh : MockThrelte) }),
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

const mat = { size: [16, 9] } as never;

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
});
