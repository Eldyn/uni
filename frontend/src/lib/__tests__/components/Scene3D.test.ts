import { describe, it, expect, vi, afterEach } from "vitest";
import MockThrelte from "./MockThrelte.svelte";
import MockAllCards3D from "./MockAllCards3D.svelte";

let preloadResolve: () => void;
let preloadPromise: Promise<void>;

function resetPreload() {
	preloadPromise = new Promise<void>((resolve) => {
		preloadResolve = resolve;
	});
}
resetPreload();

vi.mock("@threlte/core", () => ({
	T: new Proxy(
		{},
		{
			get: () => MockThrelte
		}
	),
	useTask: vi.fn(),
	useThrelte: () => ({ camera: { current: {} } })
}));

vi.mock("@threlte/extras", () => ({
	HTML: MockThrelte,
	interactivity: vi.fn(),
	useInteractivity: () => ({ raycaster: { camera: null } })
}));

vi.mock("$components/game/three/cardFaceAtlas", () => ({
	preloadCardArt: vi.fn(() => preloadPromise)
}));

vi.mock("$components/game/three/AllCards3D.svelte", () => ({
	default: MockAllCards3D
}));

import { render, cleanup } from "@testing-library/svelte";
import Scene3D from "$components/game/three/Scene3D.svelte";
import { computeSceneGeometry } from "$components/game/layout/sceneGeometry";
import { CardBus, CARD_BUS_KEY } from "$components/game/card-bus.svelte";
import { CardRegistry, CARD_REGISTRY_KEY } from "$components/game/animation/cardRegistry.svelte";
import { preloadCardArt } from "$components/game/three/cardFaceAtlas";
import { tick } from "svelte";

describe("Scene3D card art preloading", () => {
	afterEach(() => {
		cleanup();
		resetPreload();
	});

	it("calls preloadCardArt and only mounts AllCards3D after resolution", async () => {
		const bus = new CardBus();
		const registry = new CardRegistry();
		const viewport = { width: 1200, height: 800, aspect: 1.5, orientation: "landscape" as const };
		const geometry = computeSceneGeometry(viewport, 0);

		const { queryByTestId } = render(Scene3D, {
			props: {
				mappedOpponents: [],
				viewport,
				geometry,
				colorFor: () => "#ff0000",
				selectedId: null,
				onSelectionChange: vi.fn(),
				onPlay: vi.fn()
			},
			context: new Map<any, any>([
				[CARD_BUS_KEY, bus],
				[CARD_REGISTRY_KEY, registry]
			])
		});

		expect(preloadCardArt).toHaveBeenCalled();
		// Before preload resolution, AllCards3D should not be mounted
		expect(queryByTestId("all-cards-3d")).toBeNull();

		// Resolve preload promise
		preloadResolve();
		await tick();
		await tick();

		// After resolution, AllCards3D is mounted
		expect(queryByTestId("all-cards-3d")).not.toBeNull();
	});
});
