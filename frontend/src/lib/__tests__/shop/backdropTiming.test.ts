import { afterEach, describe, it, expect, vi } from "vitest";
import {
	BACKDROP_FRAME_INTERVAL_MS,
	BACKDROP_MAX_FPS,
	BACKDROP_STILL_TIME_S,
	prefersStillBackdrop,
	shouldDrawFrame
} from "$lib/shop/backdropTiming";

function stubMatchMedia(matching: string[]): void {
	vi.stubGlobal(
		"matchMedia",
		vi.fn((query: string) => ({ matches: matching.includes(query), media: query }))
	);
}

describe("backdropTiming", () => {
	afterEach(() => {
		vi.unstubAllGlobals();
	});

	it("caps the draw rate at about 20 fps", () => {
		expect(BACKDROP_MAX_FPS).toBe(20);
		expect(BACKDROP_FRAME_INTERVAL_MS).toBe(50);
	});

	it("always draws the first frame", () => {
		expect(shouldDrawFrame(1000, Number.NEGATIVE_INFINITY)).toBe(true);
	});

	it("skips a frame that comes too soon", () => {
		expect(shouldDrawFrame(1010, 1000)).toBe(false);
		expect(shouldDrawFrame(1049, 1000)).toBe(false);
	});

	it("draws again once the interval has passed", () => {
		expect(shouldDrawFrame(1050, 1000)).toBe(true);
	});

	it("uses a fixed still time", () => {
		expect(BACKDROP_STILL_TIME_S).toBe(12);
	});

	it("asks for a still frame under reduced motion", () => {
		stubMatchMedia(["(prefers-reduced-motion: reduce)"]);
		expect(prefersStillBackdrop()).toBe(true);
	});

	it("asks for a still frame on a slow-update display", () => {
		stubMatchMedia(["(update: slow)"]);
		expect(prefersStillBackdrop()).toBe(true);
	});

	it("keeps animating when no query matches", () => {
		stubMatchMedia([]);
		expect(prefersStillBackdrop()).toBe(false);
	});

	it("keeps animating when matchMedia is missing", () => {
		vi.stubGlobal("matchMedia", undefined);
		expect(prefersStillBackdrop()).toBe(false);
	});
});
