import { describe, it, expect, vi, afterEach, beforeEach } from "vitest";
import { flushSync } from "svelte";
import MockThrelte from "./MockThrelte.svelte";

vi.mock("@threlte/core", () => ({
	T: new Proxy({}, { get: () => MockThrelte }),
	useTask: vi.fn()
}));

vi.mock("$components/game/three/textures", () => ({
	loadSilhouette: vi.fn(() => new Promise(() => {}))
}));

vi.mock("$stores/audio.svelte", () => ({ storeAudio: { playSfx: vi.fn() } }));

import { render, cleanup } from "@testing-library/svelte";
import { gsap } from "gsap";
import LocalSeat3D from "$components/game/three/LocalSeat3D.svelte";
import { storeAnimation } from "$stores/animation.svelte";
import { storeAudio } from "$stores/audio.svelte";
import { storeTurnCue } from "$stores/turnCue.svelte";

const placement = { localAvatarZ: 2 } as never;

function mountSeat() {
	return render(LocalSeat3D, {
		props: {
			player: { username: "me", card_count: 3, is_bot: false },
			color: "#00ff00",
			placement,
			isTurn: true
		}
	});
}

describe("LocalSeat3D turn cue", () => {
	beforeEach(() => {
		storeTurnCue.reset();
		vi.mocked(storeAudio.playSfx).mockClear();
	});

	afterEach(() => {
		cleanup();
		storeAnimation.enabled = true;
		vi.restoreAllMocks();
	});

	it("skips the pulse and the cue sound under reduced motion", () => {
		storeAnimation.enabled = false;
		const tweenSpy = vi.spyOn(gsap, "to");
		mountSeat();
		storeTurnCue.fire();
		flushSync();
		expect(tweenSpy).not.toHaveBeenCalled();
		expect(storeAudio.playSfx).not.toHaveBeenCalled();
	});

	it("starts the pulse and plays the cue sound when animations are on", () => {
		storeAnimation.enabled = true;
		const tweenSpy = vi.spyOn(gsap, "to");
		mountSeat();
		storeTurnCue.fire();
		flushSync();
		expect(tweenSpy).toHaveBeenCalledTimes(1);
		expect(storeAudio.playSfx).toHaveBeenCalledWith("sfx.turn.start");
	});
});
