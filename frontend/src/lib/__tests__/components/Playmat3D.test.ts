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
