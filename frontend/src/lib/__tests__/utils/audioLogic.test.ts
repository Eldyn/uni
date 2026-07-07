import { describe, it, expect, vi, afterEach } from "vitest";
import {
	pickVariant,
	randomPitch,
	resolveMusicForContext,
	shouldThrottle
} from "$lib/audio/audioLogic";

describe("audioLogic: pickVariant", () => {
	afterEach(() => {
		vi.restoreAllMocks();
	});

	it("picks the first variant when Math.random() returns 0", () => {
		vi.spyOn(Math, "random").mockReturnValue(0);
		expect(pickVariant(["a", "b", "c"])).toBe("a");
	});

	it("picks the last variant when Math.random() returns just under 1", () => {
		vi.spyOn(Math, "random").mockReturnValue(0.999999);
		expect(pickVariant(["a", "b", "c"])).toBe("c");
	});

	it("throws when variants is empty", () => {
		expect(() => pickVariant([])).toThrow();
	});
});

describe("audioLogic: randomPitch", () => {
	afterEach(() => {
		vi.restoreAllMocks();
	});

	it("returns the exact value when range is a single point", () => {
		expect(randomPitch([1.5, 1.5])).toBe(1.5);
	});

	it("respects range bounds", () => {
		vi.spyOn(Math, "random").mockReturnValue(0.5);
		expect(randomPitch([1, 2])).toBe(1.5);
	});

	it("returns the lower bound when Math.random() returns 0", () => {
		vi.spyOn(Math, "random").mockReturnValue(0);
		expect(randomPitch([0.8, 1.2])).toBe(0.8);
	});
});

describe("audioLogic: resolveMusicForContext", () => {
	it("returns the catalog id for a screen with an entry", () => {
		expect(resolveMusicForContext("main")).toBe("music.fuzzsong");
	});

	it("returns undefined for a screen with no entry (the match screen — no music during gameplay)", () => {
		expect(resolveMusicForContext("game")).toBeUndefined();
	});
});

describe("audioLogic: shouldThrottle", () => {
	it("returns false when the id was never played before", () => {
		const lastPlayedAt = new Map<string, number>();
		expect(shouldThrottle("sfx.deal", lastPlayedAt, { minIntervalMs: 200 }, 1000)).toBe(false);
	});

	it("returns false when outside the throttle window", () => {
		const lastPlayedAt = new Map([["sfx.deal", 0]]);
		expect(shouldThrottle("sfx.deal", lastPlayedAt, { minIntervalMs: 200 }, 500)).toBe(false);
	});

	it("returns true when inside the throttle window", () => {
		const lastPlayedAt = new Map([["sfx.deal", 900]]);
		expect(shouldThrottle("sfx.deal", lastPlayedAt, { minIntervalMs: 200 }, 1000)).toBe(true);
	});

	it("returns false when minIntervalMs is undefined regardless of timing", () => {
		const lastPlayedAt = new Map([["sfx.deal", 999]]);
		expect(shouldThrottle("sfx.deal", lastPlayedAt, {}, 1000)).toBe(false);
	});
});
