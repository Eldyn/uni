// Playmat3D's committed colour lives in the felt's shader uniforms, not a
// plain reactive prop: the mat holds the game's active colour, except while the
// match-intro deal demands rebeccapurple (#663399). This mounts the component
// against a colour-recording Threlte stand-in so the branch can be observed
// without a real WebGL context.

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
import { DESKTOP_MAT_SHEET, PHONE_MAT_SHEET } from "$components/game/layout/playmat";
import { loopRect, sheetBlockCount } from "$components/game/three/loopGeometry";
import { LOOP_CORNER_RADIUS_BLOCKS, LOOP_INSET_BLOCKS } from "$components/game/animation/loopPlan";
import * as THREE from "three";

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

type Uniforms = Record<string, { value: unknown }>;

function uniformSets(): Uniforms[] {
	return meshInstances
		.map((instance) => (instance as unknown as { uniforms?: Uniforms }).uniforms)
		.filter((uniforms): uniforms is Uniforms => Boolean(uniforms));
}

function feltUniforms(): Uniforms {
	const felt = uniformSets().find((uniforms) => !("uLoopLength" in uniforms));
	if (!felt) throw new Error("felt material not mounted");
	return felt;
}

function hexOf(css: string): number {
	return new THREE.Color(css).getHex();
}

function feltTint(): number {
	return (feltUniforms().uToColor.value as THREE.Color).getHex();
}

describe("Playmat3D match-intro tint", () => {
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

	it("keeps the mat at #663399 while the intro forces it, regardless of active_type", async () => {
		storeMatchIntro.forcePurpleMat = true;
		render(Playmat3D, { props: { mat, viewport } });
		await flush();

		expect(feltTint()).toBe(hexOf("#663399"));
		expect(feltTint()).not.toBe(hexOf(CARD_COLOR_MAP.red));
	});

	it("ripples from the forced purple into the first real colour when the intro clears", async () => {
		const start = vi.spyOn(storeMatRipple, "startMatRipple").mockImplementation((color: string) => {
			storeMatRipple.committedColor = color;
			storeMatRipple.active = false;
		});

		render(Playmat3D, { props: { mat, viewport } });
		await flush();
		expect(feltTint()).toBe(hexOf(CARD_COLOR_MAP.red));

		storeMatchIntro.forcePurpleMat = true;
		await flush();
		expect(feltTint()).toBe(hexOf("#663399"));

		storeMatchIntro.forcePurpleMat = false;
		await flush();
		// The release sweeps from the mat's centre rather than snapping.
		expect(start).toHaveBeenCalledWith(
			CARD_COLOR_MAP.red,
			"normal",
			{ u: 0.5, v: 0.5 },
			expect.any(Number)
		);
		expect(feltTint()).toBe(hexOf(CARD_COLOR_MAP.red));
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
		expect(feltTint()).toBe(hexOf(CARD_COLOR_MAP.red));
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
		vi.mocked(useTask).mockClear();
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

	function feltTaskCallback(): () => void {
		const calls = vi.mocked(useTask).mock.calls;
		return calls[0][0] as unknown as () => void;
	}

	it("writes the sweep colours, active flag and a growing radius into the felt each step", async () => {
		const nowSpy = vi.spyOn(performance, "now");
		nowSpy.mockReturnValue(0);
		render(Playmat3D, { props: { mat, viewport } });
		await flush();
		const step = feltTaskCallback();

		const start = 1000;
		storeMatRipple.active = true;
		storeMatRipple.strength = "normal";
		storeMatRipple.fromColor = CARD_COLOR_MAP.red;
		storeMatRipple.toColor = CARD_COLOR_MAP.blue;
		storeMatRipple.committedColor = CARD_COLOR_MAP.red;
		storeMatRipple.originUv = { u: 0.5, v: 0.5 };
		storeMatRipple.startTimeMs = start;
		storeMatRipple.durationMs = 480;
		storeMatRipple.maxRadius = 1;

		nowSpy.mockReturnValue(start);
		step();
		const uniforms = feltUniforms();
		expect((uniforms.uFromColor.value as THREE.Color).getHex()).toBe(hexOf(CARD_COLOR_MAP.red));
		expect((uniforms.uToColor.value as THREE.Color).getHex()).toBe(hexOf(CARD_COLOR_MAP.blue));
		expect(uniforms.uActive.value).toBe(1);
		const firstRadius = uniforms.uRadius.value as number;

		nowSpy.mockReturnValue(start + 3 * (1000 / 12));
		step();
		expect(uniforms.uRadius.value as number).toBeGreaterThan(firstRadius);
	});

	it("resets the felt to idle once the ripple completes", async () => {
		const nowSpy = vi.spyOn(performance, "now");
		nowSpy.mockReturnValue(0);

		// active_type already matches the ripple's toColor here, as it does in
		// the real app: a ripple only ever starts because active_type just
		// changed, so by the time it finishes storeGame.state has long since
		// caught up to the same colour the ripple is sweeping toward.
		storeGame.state = { active_type: "blue", play_direction: 1 } as never;
		render(Playmat3D, { props: { mat, viewport } });
		await flush();
		const step = feltTaskCallback();

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
		expect(feltUniforms().uActive.value).toBe(1);

		// Completion: matRipple.svelte.ts's own #finish() sets committedColor
		// and active=false together (its setTimeout normally drives this; here
		// it's simulated directly, matching the existing "does not let a store
		// colour change overwrite an active ripple" test's approach elsewhere
		// in this file). The idle effect, not the frame task, is what resets
		// the uniforms in response.
		storeMatRipple.committedColor = storeMatRipple.toColor;
		storeMatRipple.active = false;
		await flush();

		expect(feltUniforms().uActive.value).toBe(0);
		expect((feltUniforms().uFromColor.value as THREE.Color).getHex()).toBe(
			hexOf(CARD_COLOR_MAP.blue)
		);
		expect((feltUniforms().uToColor.value as THREE.Color).getHex()).toBe(
			hexOf(CARD_COLOR_MAP.blue)
		);

		// A further frame task tick while inactive must stay a no-op (it
		// returns immediately on !ripple.active), so nothing regresses back
		// toward the old colour.
		nowSpy.mockReturnValue(start + 20 * (1000 / 12));
		step();
		await flush();
		expect(feltUniforms().uActive.value).toBe(0);
		expect((feltUniforms().uFromColor.value as THREE.Color).getHex()).toBe(
			hexOf(CARD_COLOR_MAP.blue)
		);
		expect((feltUniforms().uToColor.value as THREE.Color).getHex()).toBe(
			hexOf(CARD_COLOR_MAP.blue)
		);
	});
});

describe("Playmat3D turn loop", () => {
	beforeEach(() => {
		resetMockState();
		storeGame.state = { active_type: "red", play_direction: 1 } as never;
	});

	afterEach(() => {
		cleanup();
		storeGame.state = null;
		storeMatchIntro.end();
		storeMatRipple.reset();
	});

	it("hands the loop the felt's own ripple uniform objects", async () => {
		render(Playmat3D, { props: { mat, viewport } });
		await flush();
		const felt = uniformSets().find((uniforms) => !("uLoopLength" in uniforms));
		const loop = uniformSets().find((uniforms) => "uLoopLength" in uniforms);
		expect(felt).toBeDefined();
		expect(loop).toBeDefined();
		expect(loop!.uFromColor).toBe(felt!.uFromColor);
		expect(loop!.uRadius).toBe(felt!.uRadius);
		expect(loop!.uBlockCount).toBe(felt!.uBlockCount);
	});

	it("starts the felt on the real block grid, before any texture has loaded", async () => {
		render(Playmat3D, { props: { mat, viewport } });
		await flush();
		const felt = uniformSets().find((uniforms) => !("uLoopLength" in uniforms))!;
		const blockCount = felt.uBlockCount.value as THREE.Vector2;
		expect(blockCount.x).toBe(sheetBlockCount(DESKTOP_MAT_SHEET).x);
		expect(blockCount.y).toBe(sheetBlockCount(DESKTOP_MAT_SHEET).y);
	});

	it("follows the desktop felt box on a landscape viewport and the phone box on a phone", async () => {
		render(Playmat3D, { props: { mat, viewport } });
		await flush();
		let loop = uniformSets().find((uniforms) => "uLoopLength" in uniforms)!;
		const desktop = loopRect(
			sheetBlockCount(DESKTOP_MAT_SHEET),
			DESKTOP_MAT_SHEET.feltUvRect,
			LOOP_INSET_BLOCKS,
			LOOP_CORNER_RADIUS_BLOCKS
		);
		expect((loop.uLoopHalfSize.value as THREE.Vector2).x).toBeCloseTo(desktop.halfWidth);
		cleanup();
		resetMockState();

		const phoneViewport: ViewportInfo = { width: 390, height: 844, orientation: "portrait" };
		render(Playmat3D, { props: { mat, viewport: phoneViewport } });
		await flush();
		loop = uniformSets().find((uniforms) => "uLoopLength" in uniforms)!;
		const phone = loopRect(
			sheetBlockCount(PHONE_MAT_SHEET),
			PHONE_MAT_SHEET.feltUvRect,
			LOOP_INSET_BLOCKS,
			LOOP_CORNER_RADIUS_BLOCKS
		);
		expect((loop.uLoopHalfSize.value as THREE.Vector2).x).toBeCloseTo(phone.halfWidth);
	});
});
