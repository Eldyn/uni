import { describe, it, expect, vi, afterEach } from "vitest";
import MockThrelte from "./MockThrelte.svelte";
import MockAllCards3D from "./MockAllCards3D.svelte";

let preloadResolve: () => void;
let preloadReject: (err: unknown) => void;
let preloadPromise: Promise<void>;

function resetPreload() {
	preloadPromise = new Promise<void>((resolve, reject) => {
		preloadResolve = resolve;
		preloadReject = reject;
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
	useThrelte: () => ({ camera: { current: {} }, invalidate: vi.fn() })
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
import { storeGame } from "$lib/stores/game.svelte";
import { tick } from "svelte";

describe("Scene3D card art preloading", () => {
	afterEach(() => {
		cleanup();
		resetPreload();
		vi.restoreAllMocks();
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

	it("emits match_client_ready once, and only after preload resolves", async () => {
		const readySpy = vi.spyOn(storeGame, "sendClientReady").mockImplementation(() => {});
		const bus = new CardBus();
		const registry = new CardRegistry();
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
			context: new Map<any, any>([
				[CARD_BUS_KEY, bus],
				[CARD_REGISTRY_KEY, registry]
			])
		});

		await tick();
		// The ready signal must wait for card art, not fire on mount.
		expect(readySpy).not.toHaveBeenCalled();

		preloadResolve();
		await tick();
		await tick();

		expect(readySpy).toHaveBeenCalledTimes(1);
	});

	it("does not emit match_client_ready when unmounted before preload resolves", async () => {
		const readySpy = vi.spyOn(storeGame, "sendClientReady").mockImplementation(() => {});
		const viewport = { width: 1200, height: 800, aspect: 1.5, orientation: "landscape" as const };

		const { unmount } = render(Scene3D, {
			props: {
				mappedOpponents: [],
				viewport,
				geometry: computeSceneGeometry(viewport, 0),
				colorFor: () => "#ff0000",
				selectedId: null,
				onSelectionChange: vi.fn(),
				onPlay: vi.fn()
			},
			context: new Map<any, any>([
				[CARD_BUS_KEY, new CardBus()],
				[CARD_REGISTRY_KEY, new CardRegistry()]
			])
		});

		unmount();
		preloadResolve();
		await tick();
		await tick();

		// The cancelled guard must win: a torn-down scene never signals ready.
		expect(readySpy).not.toHaveBeenCalled();
	});

	it("signals ready even when card art preload rejects", async () => {
		const readySpy = vi.spyOn(storeGame, "sendClientReady").mockImplementation(() => {});
		const errorSpy = vi.spyOn(console, "error").mockImplementation(() => {});
		const viewport = { width: 1200, height: 800, aspect: 1.5, orientation: "landscape" as const };

		render(Scene3D, {
			props: {
				mappedOpponents: [],
				viewport,
				geometry: computeSceneGeometry(viewport, 0),
				colorFor: () => "#ff0000",
				selectedId: null,
				onSelectionChange: vi.fn(),
				onPlay: vi.fn()
			},
			context: new Map<any, any>([
				[CARD_BUS_KEY, new CardBus()],
				[CARD_REGISTRY_KEY, new CardRegistry()]
			])
		});

		await tick();
		expect(readySpy).not.toHaveBeenCalled();

		preloadReject(new Error("preload boom"));
		await tick();
		await tick();

		// A failed preload must not strand the barrier: ready is still sent.
		expect(errorSpy).toHaveBeenCalled();
		expect(readySpy).toHaveBeenCalledTimes(1);
	});

	it("does not emit match_client_ready when a rejected preload settles after unmount", async () => {
		const readySpy = vi.spyOn(storeGame, "sendClientReady").mockImplementation(() => {});
		vi.spyOn(console, "error").mockImplementation(() => {});
		const viewport = { width: 1200, height: 800, aspect: 1.5, orientation: "landscape" as const };

		const { unmount } = render(Scene3D, {
			props: {
				mappedOpponents: [],
				viewport,
				geometry: computeSceneGeometry(viewport, 0),
				colorFor: () => "#ff0000",
				selectedId: null,
				onSelectionChange: vi.fn(),
				onPlay: vi.fn()
			},
			context: new Map<any, any>([
				[CARD_BUS_KEY, new CardBus()],
				[CARD_REGISTRY_KEY, new CardRegistry()]
			])
		});

		unmount();
		preloadReject(new Error("preload boom"));
		await tick();
		await tick();

		// The cancelled guard must win over the rejection path too.
		expect(readySpy).not.toHaveBeenCalled();
	});
});
