// Playmat3D's tint is a plain reactive value: the mat holds the game's active
// colour, except while the match-intro deal demands rebeccapurple (#663399).
// This mounts the component against a colour-recording Threlte stand-in so the
// branch can be observed without a real WebGL context.

import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import MockTintMaterial from "./MockTintMaterial.svelte";
import { meshInstances, resetMockState } from "./drawPileMockState";

vi.mock("@threlte/core", () => ({
	T: new Proxy({}, { get: () => MockTintMaterial })
}));

vi.mock("$components/game/three/textures", () => ({
	loadTexture: vi.fn().mockResolvedValue({})
}));

import { render, cleanup } from "@testing-library/svelte";
import Playmat3D from "$components/game/three/Playmat3D.svelte";
import { storeGame } from "$stores/game.svelte";
import { storeMatchIntro } from "$stores/matchIntro.svelte";
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
		expect(tints().at(-1)).toBe("#663399");

		storeMatchIntro.forcePurpleMat = false;
		await flush();
		expect(tints().slice(-2)).toEqual([CARD_COLOR_MAP.red, CARD_COLOR_MAP.red]);
	});
});
