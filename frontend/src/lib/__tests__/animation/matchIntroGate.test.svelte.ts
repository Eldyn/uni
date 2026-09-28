import { describe, it, expect, beforeEach } from "vitest";
import { flushSync } from "svelte";
import { shouldStartMatchIntro } from "$components/game/animation/matchIntroGate";
import { storeGameLoader } from "$lib/stores/gameLoader.svelte";

describe("shouldStartMatchIntro", () => {
	beforeEach(() => {
		storeGameLoader.shown = false;
	});

	it("does not start while the loader is shown", () => {
		expect(shouldStartMatchIntro({ hasState: true, introReady: true, loaderShown: true })).toBe(
			false
		);
	});

	it("starts once the loader hides", () => {
		expect(shouldStartMatchIntro({ hasState: true, introReady: true, loaderShown: false })).toBe(
			true
		);
	});

	it("does not start before the ready barrier opens, even with the loader hidden", () => {
		expect(shouldStartMatchIntro({ hasState: true, introReady: false, loaderShown: false })).toBe(
			false
		);
	});

	it("does not start without a state snapshot", () => {
		expect(shouldStartMatchIntro({ hasState: false, introReady: true, loaderShown: false })).toBe(
			false
		);
	});

	it("re-runs when the loader hides, starting the intro only then", () => {
		storeGameLoader.shown = true;
		const started: boolean[] = [];
		const stop = $effect.root(() => {
			$effect(() => {
				started.push(
					shouldStartMatchIntro({
						hasState: true,
						introReady: true,
						loaderShown: storeGameLoader.shown
					})
				);
			});
		});
		flushSync();
		expect(started).toEqual([false]);

		storeGameLoader.shown = false;
		flushSync();
		expect(started).toEqual([false, true]);

		stop();
	});
});
