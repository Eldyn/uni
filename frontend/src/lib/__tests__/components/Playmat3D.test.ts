// Playmat3D's tint is a plain reactive value: the mat holds the game's active
// colour, except while the match-intro deal demands rebeccapurple (#663399).
// This mounts the component against a colour-recording Threlte stand-in so the
// branch can be observed without a real WebGL context.

import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import MockTintMaterial from "./MockTintMaterial.svelte";
import { meshInstances, resetMockState } from "./drawPileMockState";

vi.mock("@threlte/core", () => ({
	T: new Proxy({}, { get: () => MockTintMaterial }),
	useTask: vi.fn(),
	useThrelte: () => ({ invalidate: vi.fn() })
}));

vi.mock("$components/game/three/textures", () => ({
	loadTexture: vi.fn().mockResolvedValue({})
}));

import { render, cleanup } from "@testing-library/svelte";
import { useTask } from "@threlte/core";
import Playmat3D from "$components/game/three/Playmat3D.svelte";
import { storeGame } from "$stores/game.svelte";
import { storeMatchIntro } from "$stores/matchIntro.svelte";
import { storeMatRipple, MAT_INITIAL_COLOR } from "$components/game/three/ripple/matRipple.svelte";
import { CARD_COLOR_MAP } from "$lib/palette";
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

/** Every colour that reached a material, in the order the effects recorded it. */
function tints(): unknown[] {
	return meshInstances.map((m) => m.color).filter((c) => typeof c === "string");
}

describe("Playmat3D match-intro tint", () => {
	beforeEach(() => {
		resetMockState();
		storeGame.state = { active_type: "red", play_direction: 1 } as never;
	});

	afterEach(() => {
		cleanup();
		storeGame.state = null;
		storeMatchIntro.end();
		storeMatRipple.active = false;
		storeMatRipple.committedColor = MAT_INITIAL_COLOR;
		storeMatRipple.fromColor = MAT_INITIAL_COLOR;
		storeMatRipple.toColor = MAT_INITIAL_COLOR;
	});

	it("keeps the mat at #663399 while the intro forces it, regardless of active_type", async () => {
		storeMatchIntro.forcePurpleMat = true;
		render(Playmat3D, { props: { mat, viewport } });
		await flush();

		expect(tints()).toContain("#663399");
		expect(tints()).not.toContain(CARD_COLOR_MAP.red);
	});

	it("resumes normal tinting once the intro clears", async () => {
		render(Playmat3D, { props: { mat, viewport } });
		await flush();
		expect(tints()).toContain(CARD_COLOR_MAP.red);

		storeMatchIntro.forcePurpleMat = true;
		await flush();
		const afterPurple = tints();
		expect(afterPurple[afterPurple.length - 1]).toBe("#663399");

		storeMatchIntro.forcePurpleMat = false;
		await flush();
		// The felt plane is now a ShaderMaterial (its colour lives in uniforms,
		// not a recordable `color` prop), so only the arrows plane still pushes
		// through MockTintMaterial — one entry per change, not two.
		const afterClear = tints();
		expect(afterClear[afterClear.length - 1]).toBe(CARD_COLOR_MAP.red);
	});
});

describe("Playmat3D ripple colour sync", () => {
	beforeEach(() => {
		resetMockState();
		storeGame.state = { active_type: "red", play_direction: 1 } as never;
	});

	afterEach(() => {
		cleanup();
		storeGame.state = null;
		storeMatchIntro.end();
		storeMatRipple.active = false;
		storeMatRipple.committedColor = MAT_INITIAL_COLOR;
		storeMatRipple.fromColor = MAT_INITIAL_COLOR;
		storeMatRipple.toColor = MAT_INITIAL_COLOR;
	});

	it("syncs the idle mat to storeGame.active_type through storeMatRipple.syncColor", async () => {
		const syncColor = vi.spyOn(storeMatRipple, "syncColor");
		render(Playmat3D, { props: { mat, viewport } });
		await flush();

		expect(syncColor).toHaveBeenCalledWith(CARD_COLOR_MAP.red);
		expect(storeMatRipple.committedColor).toBe(CARD_COLOR_MAP.red);
		expect(tints()).toContain(CARD_COLOR_MAP.red);
	});

	it("does not let a store colour change overwrite an active ripple", async () => {
		render(Playmat3D, { props: { mat, viewport } });
		await flush();

		// A ripple already sweeping from red to blue, mid-flight.
		storeMatRipple.active = true;
		storeMatRipple.fromColor = CARD_COLOR_MAP.red;
		storeMatRipple.toColor = CARD_COLOR_MAP.blue;
		storeMatRipple.committedColor = CARD_COLOR_MAP.red;
		storeMatRipple.startTimeMs = performance.now();
		storeMatRipple.durationMs = 1000;
		storeMatRipple.maxRadius = 1;
		await flush();

		// The active_type store changes again mid-sweep (a fast wild chain);
		// the ripple's own from/to must not be stomped by the new colour.
		storeGame.state = { active_type: "green", play_direction: 1 } as never;
		await flush();

		expect(storeMatRipple.active).toBe(true);
		expect(storeMatRipple.fromColor).toBe(CARD_COLOR_MAP.red);
		expect(storeMatRipple.toColor).toBe(CARD_COLOR_MAP.blue);
		expect(storeMatRipple.committedColor).toBe(CARD_COLOR_MAP.red);
	});
});

// Drives the per-frame useTask callback directly with a controlled clock
// (a mocked performance.now, since useTask: vi.fn() never auto-invokes it),
// rather than waiting on real frames — the same technique the component
// itself uses via storeMatRipple's injectable now().
describe("Playmat3D ripple frame stepping", () => {
	beforeEach(() => {
		resetMockState();
		storeGame.state = { active_type: "red", play_direction: 1 } as never;
	});

	afterEach(() => {
		cleanup();
		vi.restoreAllMocks();
		storeGame.state = null;
		storeMatchIntro.end();
		storeMatRipple.active = false;
		storeMatRipple.committedColor = MAT_INITIAL_COLOR;
		storeMatRipple.fromColor = MAT_INITIAL_COLOR;
		storeMatRipple.toColor = MAT_INITIAL_COLOR;
	});

	function latestTaskCallback(): () => void {
		const calls = vi.mocked(useTask).mock.calls;
		return calls[calls.length - 1][0] as unknown as () => void;
	}

	it("switches the arrows tint to the target colour once the front passes half radius", async () => {
		const nowSpy = vi.spyOn(performance, "now");
		nowSpy.mockReturnValue(0);

		render(Playmat3D, { props: { mat, viewport } });
		await flush();
		const step = latestTaskCallback();

		const start = 1000;
		storeMatRipple.active = true;
		storeMatRipple.strength = "normal";
		storeMatRipple.fromColor = CARD_COLOR_MAP.red;
		storeMatRipple.toColor = CARD_COLOR_MAP.blue;
		storeMatRipple.committedColor = CARD_COLOR_MAP.red;
		storeMatRipple.originUv = { u: 0.5, v: 0.5 };
		storeMatRipple.startTimeMs = start;
		storeMatRipple.durationMs = 480; // step-aligned: 480ms / (1000/12) = 5.76 steps -> 6 steps
		storeMatRipple.maxRadius = 1;

		// First step: front radius is 1/6 of maxRadius, well under half.
		nowSpy.mockReturnValue(start + 0);
		step();
		await flush();
		let beforeHalf = tints();
		expect(beforeHalf[beforeHalf.length - 1]).toBe(CARD_COLOR_MAP.red);

		// A later step whose radius crosses maxRadius / 2 (progress >= 0.5,
		// i.e. at least the 3rd of 6 steps).
		nowSpy.mockReturnValue(start + 3 * (1000 / 12));
		step();
		await flush();
		const afterHalf = tints();
		expect(afterHalf[afterHalf.length - 1]).toBe(CARD_COLOR_MAP.blue);
	});

	it("resets the felt and arrows to idle once the ripple completes", async () => {
		const nowSpy = vi.spyOn(performance, "now");
		nowSpy.mockReturnValue(0);

		// active_type already matches the ripple's toColor here, as it does in
		// the real app: a ripple only ever starts because active_type just
		// changed, so by the time it finishes storeGame.state has long since
		// caught up to the same colour the ripple is sweeping toward.
		storeGame.state = { active_type: "blue", play_direction: 1 } as never;
		render(Playmat3D, { props: { mat, viewport } });
		await flush();
		const step = latestTaskCallback();

		const start = 2000;
		storeMatRipple.active = true;
		storeMatRipple.strength = "normal";
		storeMatRipple.fromColor = CARD_COLOR_MAP.red;
		storeMatRipple.toColor = CARD_COLOR_MAP.blue;
		storeMatRipple.committedColor = CARD_COLOR_MAP.red;
		storeMatRipple.originUv = { u: 0.5, v: 0.5 };
		storeMatRipple.startTimeMs = start;
		storeMatRipple.durationMs = 480;
		storeMatRipple.maxRadius = 1;

		nowSpy.mockReturnValue(start + 5 * (1000 / 12));
		step();
		await flush();
		const midSweep = tints();
		expect(midSweep[midSweep.length - 1]).toBe(CARD_COLOR_MAP.blue);

		// Completion: matRipple.svelte.ts's own #finish() sets committedColor
		// and active=false together (its setTimeout normally drives this; here
		// it's simulated directly, matching the existing "does not let a store
		// colour change overwrite an active ripple" test's approach elsewhere
		// in this file). The idle effect, not the frame task, is what resets
		// the uniforms/arrowsTint in response.
		storeMatRipple.committedColor = storeMatRipple.toColor;
		storeMatRipple.active = false;
		await flush();

		const afterFinish = tints();
		expect(afterFinish[afterFinish.length - 1]).toBe(CARD_COLOR_MAP.blue);

		// A further frame task tick while inactive must stay a no-op (it
		// returns immediately on !ripple.active), so nothing regresses back
		// toward the old colour.
		nowSpy.mockReturnValue(start + 20 * (1000 / 12));
		step();
		await flush();
		const afterExtraTick = tints();
		expect(afterExtraTick[afterExtraTick.length - 1]).toBe(CARD_COLOR_MAP.blue);
	});
});
