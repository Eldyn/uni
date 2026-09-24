import { describe, it, expect } from "vitest";
import { gsap } from "gsap";
import {
	normalizeStepResult,
	isEffectHandle,
	noopEffectHandle,
	type EffectHandle
} from "$components/game/animation/effectHandle";

describe("effectHandle", () => {
	it("wraps a bare Timeline as a non-persistent blocking handle", () => {
		const timeline = gsap.timeline();
		timeline.to({ value: 0 }, { value: 1, duration: 1 });

		const handle = normalizeStepResult(timeline);

		expect(handle.persistent).toBe(false);
		expect(handle.blocking).toBe(timeline);
		handle.skip();
		expect(timeline.progress()).toBe(1);
	});

	it("returns an already-normalized EffectHandle unchanged", () => {
		let skipped = false;
		const handle: EffectHandle = {
			skip: () => {
				skipped = true;
			},
			blocking: null,
			persistent: true
		};

		expect(normalizeStepResult(handle)).toBe(handle);
	});

	it("isEffectHandle tells handles apart from bare timelines", () => {
		expect(isEffectHandle(gsap.timeline())).toBe(false);
		expect(isEffectHandle({ skip() {}, blocking: null, persistent: false })).toBe(true);
	});

	it("noopEffectHandle is a non-persistent, non-blocking, safe-to-skip handle", () => {
		const handle = noopEffectHandle();
		expect(handle.blocking).toBeNull();
		expect(handle.persistent).toBe(false);
		expect(() => handle.skip()).not.toThrow();
	});
});
