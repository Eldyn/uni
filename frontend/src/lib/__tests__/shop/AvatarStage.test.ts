// The 3D avatar stage: the WebGL fallback, the baked strip texture, the static
// pose and the 5-frame bob clock. Threlte is mocked the way the other three
// tests mock it (T as a passthrough component), and the texture helper is a
// fake so the component runs without a real bake.

import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { cleanup, fireEvent, render } from "@testing-library/svelte";
import { flushSync } from "svelte";
import MockThrelte from "$lib/__tests__/components/MockThrelte.svelte";
import { defaultLoadout } from "$lib/avatar/defaultLoadout";
import type { Loadout } from "$lib/avatar/types";

vi.mock("@threlte/core", () => ({
	Canvas: MockThrelte,
	T: new Proxy(
		{},
		{
			get: () => MockThrelte
		}
	)
}));

const { loadAvatarTexture } = vi.hoisted(() => ({
	loadAvatarTexture: vi.fn()
}));

vi.mock("$components/game/three/avatarTexture", () => ({ loadAvatarTexture }));

// The fallback Avatar must not start a real bake in jsdom.
vi.mock("$lib/avatar/useBakedAvatar.svelte", () => ({
	createBakedAvatar: () => ({ url: null, set: vi.fn(), destroy: vi.fn() })
}));

import AvatarStage from "$components/shop/AvatarStage.svelte";

interface FakeTexture {
	offset: { x: number };
	dispose: ReturnType<typeof vi.fn>;
	needsUpdate: boolean;
}

function makeTexture(): FakeTexture {
	return { offset: { x: 0 }, dispose: vi.fn(), needsUpdate: false };
}

function mockContext(present: boolean): void {
	vi.spyOn(HTMLCanvasElement.prototype, "getContext").mockImplementation(function (
		this: HTMLCanvasElement,
		type: string
	) {
		return (present && type === "webgl2" ? {} : null) as unknown as RenderingContext | null;
	} as unknown as HTMLCanvasElement["getContext"]);
}

function settle(): Promise<void> {
	return new Promise((resolve) => setTimeout(resolve, 0));
}

let rafCallbacks: FrameRequestCallback[] = [];

function runFrame(timeMs: number): void {
	const callback = rafCallbacks[rafCallbacks.length - 1];
	callback(timeMs);
	flushSync();
}

function loadout(skin: string): Loadout {
	return { ...defaultLoadout(), skin };
}

beforeEach(() => {
	loadAvatarTexture.mockReset();
	loadAvatarTexture.mockImplementation(async () => makeTexture());
	rafCallbacks = [];
	vi.stubGlobal(
		"requestAnimationFrame",
		vi.fn((callback: FrameRequestCallback) => {
			rafCallbacks.push(callback);
			return rafCallbacks.length;
		})
	);
	vi.stubGlobal("cancelAnimationFrame", vi.fn());
});

afterEach(() => {
	cleanup();
	vi.restoreAllMocks();
	vi.unstubAllGlobals();
	delete (document as { hidden?: boolean }).hidden;
});

describe("AvatarStage", () => {
	it("renders the fallback Avatar and no canvas without WebGL", () => {
		mockContext(false);

		const { container } = render(AvatarStage, { props: { loadout: loadout("basic:red") } });
		flushSync();

		expect(container.querySelector("canvas")).toBeNull();
		expect(container.querySelector(".avatar-stage-fallback")).toBeTruthy();
		expect(container.querySelector(".avatar-placeholder")).toBeTruthy();
		expect(requestAnimationFrame).not.toHaveBeenCalled();
	});

	it("shows the fallback sprite until the baked texture is ready", async () => {
		mockContext(true);
		let resolveTexture!: (value: FakeTexture) => void;
		loadAvatarTexture.mockImplementation(
			() =>
				new Promise<FakeTexture>((resolve) => {
					resolveTexture = resolve;
				})
		);

		const { container } = render(AvatarStage, { props: { loadout: loadout("basic:red") } });
		flushSync();

		expect(container.querySelector(".avatar-stage-fallback")).toBeTruthy();
		expect(container.querySelector(".avatar-placeholder")).toBeTruthy();

		resolveTexture(makeTexture());
		await settle();
		flushSync();

		expect(container.querySelector(".avatar-stage-fallback")).toBeNull();
	});

	it("loads a texture for the loadout and reloads it when the loadout changes", async () => {
		mockContext(true);
		const first = loadout("basic:red");
		const second = loadout("basic:blue");

		const { rerender } = render(AvatarStage, { props: { loadout: first } });
		await settle();

		expect(loadAvatarTexture).toHaveBeenCalledWith(first, false);

		await rerender({ loadout: second });
		await settle();

		expect(loadAvatarTexture).toHaveBeenCalledWith(second, false);
	});

	it("passes the bot flag to the texture helper", async () => {
		mockContext(true);

		render(AvatarStage, { props: { loadout: null, bot: true } });
		await settle();

		expect(loadAvatarTexture).toHaveBeenCalledWith(null, true);
	});

	it("is a static pose: dragging does not turn the avatar", async () => {
		mockContext(true);

		const { container } = render(AvatarStage, { props: { loadout: loadout("basic:red") } });
		flushSync();
		const stage = container.querySelector<HTMLElement>(".avatar-stage")!;

		await fireEvent.pointerDown(stage, { button: 0, clientX: 100 });
		await fireEvent.pointerMove(stage, { clientX: 900 });
		expect(stage.hasAttribute("data-angle")).toBe(false);

		await fireEvent.pointerUp(stage);
	});

	it("advances the frame on the 120 ms clock and stops when hidden", async () => {
		mockContext(true);

		const { container } = render(AvatarStage, { props: { loadout: loadout("basic:red") } });
		flushSync();
		await settle();

		const texture = (await loadAvatarTexture.mock.results[0].value) as FakeTexture;
		const stage = container.querySelector<HTMLElement>(".avatar-stage")!;
		expect(stage.getAttribute("data-frame")).toBe("0");

		runFrame(0);
		runFrame(120);
		expect(stage.getAttribute("data-frame")).toBe("1");
		expect(texture.offset.x).toBeCloseTo(1 / 5);

		runFrame(240);
		expect(stage.getAttribute("data-frame")).toBe("2");

		const scheduled = rafCallbacks.length;
		Object.defineProperty(document, "hidden", { configurable: true, value: true });
		document.dispatchEvent(new Event("visibilitychange"));

		expect(cancelAnimationFrame).toHaveBeenCalled();
		expect(rafCallbacks.length).toBe(scheduled);
	});
});
