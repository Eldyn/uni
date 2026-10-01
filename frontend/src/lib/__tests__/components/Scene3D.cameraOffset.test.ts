import { describe, it, expect, vi, afterEach } from "vitest";
import MockThrelte from "./MockThrelte.svelte";
import MockThrelteCamera from "./MockThrelteCamera.svelte";
import MockAllCards3D from "./MockAllCards3D.svelte";

vi.mock("@threlte/core", () => ({
	T: new Proxy(
		{},
		{
			get: (_, key) => (key === "PerspectiveCamera" ? MockThrelteCamera : MockThrelte)
		}
	),
	useTask: vi.fn(),
	useThrelte: () => ({ camera: { current: {} }, invalidate: vi.fn() })
}));

vi.mock("@threlte/extras", () => ({
	HTML: MockThrelte,
	interactivity: vi.fn(),
	useInteractivity: () => ({ raycaster: { camera: null } })
}));

vi.mock("$components/game/three/cardFaceAtlas", () => ({
	preloadCardArt: vi.fn(() => new Promise(() => {}))
}));

vi.mock("$components/game/three/AllCards3D.svelte", () => ({
	default: MockAllCards3D
}));

import { flushSync } from "svelte";
import { render, cleanup, screen } from "@testing-library/svelte";
import Scene3D from "$components/game/three/Scene3D.svelte";
import { computeSceneGeometry } from "$components/game/layout/sceneGeometry";
import { cameraPunchOffset } from "$components/game/layout/cameraRig";
import { CardBus, CARD_BUS_KEY } from "$components/game/card-bus.svelte";
import { CardRegistry, CARD_REGISTRY_KEY } from "$components/game/animation/cardRegistry.svelte";
import { storeCameraOffset } from "$stores/cameraOffset.svelte";

function renderedCameraPosition(): [number, number, number] {
	return JSON.parse(screen.getByTestId("scene-camera").dataset.position!);
}

describe("Scene3D camera offset", () => {
	afterEach(() => {
		storeCameraOffset.clearOffset();
		cleanup();
		vi.restoreAllMocks();
	});

	it("moves the rendered camera by the transient offset and back", () => {
		const viewport = { width: 1200, height: 800, aspect: 1.5, orientation: "landscape" as const };
		const geometry = computeSceneGeometry(viewport, 0);
		render(Scene3D, {
			props: {
				mappedOpponents: [],
				viewport,
				geometry,
				colorFor: () => "#ff0000",
				selectedId: null,
				onSelectionChange: vi.fn(),
				onPlay: vi.fn()
			},
			context: new Map<unknown, unknown>([
				[CARD_BUS_KEY, new CardBus()],
				[CARD_REGISTRY_KEY, new CardRegistry()]
			])
		});
		flushSync();
		expect(renderedCameraPosition()).toEqual(geometry.rig.position);

		const punch = cameraPunchOffset(0.5);
		storeCameraOffset.applyOffset(punch);
		flushSync();
		const [x, y, z] = renderedCameraPosition();
		expect(x).toBeCloseTo(geometry.rig.position[0] + punch.x);
		expect(y).toBeCloseTo(geometry.rig.position[1] + punch.y);
		expect(z).toBeCloseTo(geometry.rig.position[2] + punch.z);

		storeCameraOffset.clearOffset();
		flushSync();
		expect(renderedCameraPosition()).toEqual(geometry.rig.position);
	});
});
