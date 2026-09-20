// Mirrors PlayerSeat3D's "releases its ring cards from transit when the morph
// ends" test. LocalHand3D's incoming-POV morph must release its cards' transit
// flags the moment the morph goes inactive, not only at inheritProgress === 1:
// the controller nulls `transition` in the same synchronous update that sets
// inheritProgress = 1, so a progress-1-with-a-live-morph flush never occurs.

import { describe, it, expect, vi, afterEach, beforeEach } from "vitest";
import { gsap } from "gsap";
import { tick } from "svelte";
import MockThrelte from "./MockThrelte.svelte";

vi.mock("@threlte/core", () => ({
	T: new Proxy({}, { get: () => MockThrelte }),
	useTask: vi.fn()
}));

vi.mock("@threlte/extras", () => ({
	HTML: MockThrelte
}));

vi.mock("$components/game/three/textures", () => ({
	loadTexture: vi.fn(() => new Promise(() => {}))
}));

import { render, cleanup } from "@testing-library/svelte";
import LocalHand3D from "$components/game/three/LocalHand3D.svelte";
import { computeSceneGeometry } from "$components/game/layout/sceneGeometry";
import { CardBus, CARD_BUS_KEY } from "$components/game/card-bus.svelte";
import { CardRegistry, CARD_REGISTRY_KEY } from "$components/game/animation/cardRegistry.svelte";
import { storeAnimation } from "$stores/animation.svelte";
import type { ViewportInfo } from "$components/game/layout/seatLayout";
import type { GamePlayer } from "$stores/game.svelte";

const portrait: ViewportInfo = { width: 390, height: 844, orientation: "portrait" };

const twoCards: GamePlayer = {
	username: "me",
	card_count: 2,
	is_bot: false,
	hand: [
		{ id: 1, type: "red", value: "1" },
		{ id: 2, type: "blue", value: "2" }
	]
} as unknown as GamePlayer;

function propsFor() {
	const geometry = computeSceneGeometry(portrait, 0);
	return {
		rig: geometry.rig,
		viewport: portrait,
		placement: geometry.placement,
		selectedId: null as number | null,
		onSelectionChange: vi.fn(),
		onPlay: vi.fn(),
		player: twoCards
	};
}

const morph = {
	username: "me",
	poses: [
		[9, 0, 9],
		[8, 0, 8]
	] as [number, number, number][],
	spinDegs: [180, 190],
	open: false
};

describe("LocalHand3D incoming morph", () => {
	beforeEach(() => {
		gsap.ticker.lagSmoothing(false);
	});

	afterEach(() => {
		cleanup();
		gsap.globalTimeline.clear();
	});

	it("seeds cards at the arc source and releases transit when the morph ends", async () => {
		const bus = new CardBus();
		const registry = new CardRegistry();
		const context = new Map<any, any>([
			[CARD_BUS_KEY, bus],
			[CARD_REGISTRY_KEY, registry]
		]);

		const { rerender } = render(LocalHand3D, {
			props: { ...propsFor(), handMorph: morph, inheritProgress: 0 },
			context
		});
		await tick();
		await tick();

		expect(registry.isInTransit("1")).toBe(true);
		expect(registry.isInTransit("2")).toBe(true);
		// At progress 0 the card sits at its arc source, not its hand slot.
		expect(registry.getPose("1")!.x).toBeCloseTo(9);
		expect(registry.getPose("2")!.x).toBeCloseTo(8);

		// Animation off so the post-release idle re-sync clears the flag
		// synchronously rather than handing off to a displacement tween.
		const wasEnabled = storeAnimation.enabled;
		storeAnimation.enabled = false;
		rerender({ ...propsFor(), handMorph: null, inheritProgress: 1 });
		storeAnimation.enabled = wasEnabled;
		await tick();
		await tick();

		expect(registry.isInTransit("1")).toBe(false);
		expect(registry.isInTransit("2")).toBe(false);
	});

	it("turns back → face and blurs spin to the row across inheritProgress when open", async () => {
		const bus = new CardBus();
		const registry = new CardRegistry();
		const context = new Map<any, any>([
			[CARD_BUS_KEY, bus],
			[CARD_REGISTRY_KEY, registry]
		]);
		const openMorph = { ...morph, open: true };

		const { rerender } = render(LocalHand3D, {
			props: { ...propsFor(), handMorph: openMorph, inheritProgress: 0 },
			context
		});
		await tick();
		await tick();

		// Early in the blend it is edge-on at 180° (showing its back) and still
		// carries the ring slot's in-plane spin.
		expect(registry.getPose("1")!.turned).toBe(false);
		expect(registry.getPose("1")!.flipDeg).toBeCloseTo(180);
		expect(registry.getPose("1")!.spinDeg).toBeCloseTo(180);

		rerender({ ...propsFor(), handMorph: openMorph, inheritProgress: 0.5 });
		await tick();
		await tick();

		// Half-way it has turned half-way and its spin has blended half-way.
		expect(registry.getPose("1")!.flipDeg).toBeCloseTo(90);
		expect(registry.getPose("1")!.spinDeg).toBeCloseTo(90);

		rerender({ ...propsFor(), handMorph: openMorph, inheritProgress: 1 });
		await tick();
		await tick();

		// Landed: face-up and square in the row.
		expect(registry.getPose("1")!.turned).toBe(false);
		expect(registry.getPose("1")!.flipDeg).toBeCloseTo(0);
		expect(registry.getPose("1")!.spinDeg).toBeCloseTo(0);
	});

	it("keeps a hidden (non-open) morph's cards as backs", async () => {
		const bus = new CardBus();
		const registry = new CardRegistry();
		const context = new Map<any, any>([
			[CARD_BUS_KEY, bus],
			[CARD_REGISTRY_KEY, registry]
		]);

		render(LocalHand3D, {
			props: { ...propsFor(), handMorph: morph, inheritProgress: 1 },
			context
		});
		await tick();
		await tick();

		expect(registry.getPose("1")!.turned).toBe(true);
		expect(registry.getPose("1")!.flipDeg).toBeCloseTo(0);
	});

	it("retires its registry entries on unmount", async () => {
		const bus = new CardBus();
		const registry = new CardRegistry();
		const context = new Map<any, any>([
			[CARD_BUS_KEY, bus],
			[CARD_REGISTRY_KEY, registry]
		]);

		render(LocalHand3D, { props: propsFor(), context });
		await tick();
		await tick();

		expect(registry.activeFlights.map((f) => f.id).sort()).toEqual(["1", "2"]);

		cleanup();

		// Without an onDestroy retirement the row's entries would strand and
		// AllCards3D would draw them forever.
		expect(registry.activeFlights).toHaveLength(0);
	});
});
