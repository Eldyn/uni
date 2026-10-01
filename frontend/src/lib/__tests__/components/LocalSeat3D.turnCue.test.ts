import { describe, it, expect, vi, afterEach, beforeEach } from "vitest";
import { flushSync } from "svelte";
import MockThrelte from "./MockThrelte.svelte";
import MockThrelteMesh from "./MockThrelteMesh.svelte";

vi.mock("@threlte/core", () => ({
	T: new Proxy({}, { get: (_, key) => (key === "Mesh" ? MockThrelteMesh : MockThrelte) }),
	useTask: vi.fn()
}));

vi.mock("$components/game/three/textures", () => ({
	loadSilhouette: vi.fn(() => new Promise(() => {}))
}));

vi.mock("$components/game/three/turnRimTexture", () => ({
	turnRimTexture: vi.fn(() => ({}))
}));

vi.mock("$stores/audio.svelte", () => ({ storeAudio: { playSfx: vi.fn() } }));

import { render, cleanup, screen } from "@testing-library/svelte";
import { gsap } from "gsap";
import LocalSeat3D from "$components/game/three/LocalSeat3D.svelte";
import { storeAnimation } from "$stores/animation.svelte";
import { storeAudio } from "$stores/audio.svelte";
import { storeTurnCue } from "$stores/turnCue.svelte";

const placement = { localAvatarZ: 2 } as never;

function mountSeat(isTurn = true) {
	return render(LocalSeat3D, {
		props: {
			player: { username: "me", card_count: 3, is_bot: false },
			color: "#00ff00",
			placement,
			isTurn
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

	it("shows the steady rim on the player's turn, also under reduced motion", () => {
		storeAnimation.enabled = false;
		mountSeat(true);
		flushSync();
		expect(screen.getByTestId("turn-rim")).toBeTruthy();
	});

	it("shows no rim when it is not the player's turn", () => {
		mountSeat(false);
		flushSync();
		expect(screen.queryByTestId("turn-rim")).toBeNull();
	});

	function finishPulse(tweenSpy: ReturnType<typeof vi.spyOn>): void {
		const pulse = tweenSpy.mock.results.at(-1)?.value as gsap.core.Tween;
		pulse.progress(1);
		flushSync();
	}

	it("drops the rim once the turn passes after a pulse", async () => {
		storeAnimation.enabled = true;
		const tweenSpy = vi.spyOn(gsap, "to");
		const seat = mountSeat(false);
		storeTurnCue.fire();
		flushSync();
		await seat.rerender({ isTurn: true });
		finishPulse(tweenSpy);
		expect(screen.queryByTestId("turn-rim")).not.toBeNull();

		await seat.rerender({ isTurn: false });
		expect(screen.queryByTestId("turn-rim")).toBeNull();
	});

	it("keeps following the turn across several turns", async () => {
		storeAnimation.enabled = true;
		const tweenSpy = vi.spyOn(gsap, "to");
		const seat = mountSeat(false);
		for (let turn = 0; turn < 3; turn++) {
			storeTurnCue.fire();
			flushSync();
			await seat.rerender({ isTurn: true });
			finishPulse(tweenSpy);
			await seat.rerender({ isTurn: false });
			expect(screen.queryByTestId("turn-rim")).toBeNull();
		}
	});

	it("does not restart the pulse on unrelated changes", async () => {
		storeAnimation.enabled = true;
		const tweenSpy = vi.spyOn(gsap, "to");
		const seat = mountSeat(true);
		storeTurnCue.fire();
		flushSync();
		finishPulse(tweenSpy);
		await seat.rerender({ color: "#ff0000", dimmed: true });
		storeAnimation.speedMultiplier = 2;
		flushSync();
		storeAnimation.speedMultiplier = 1;
		flushSync();
		expect(tweenSpy).toHaveBeenCalledTimes(1);
		expect(screen.queryByTestId("turn-rim")).not.toBeNull();
	});
});
