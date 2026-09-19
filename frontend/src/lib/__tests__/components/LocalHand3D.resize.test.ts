// Regression test for the abrupt-resolution-change card teleport.

// On a height-only viewport change (the mobile URL bar appearing/disappearing
// is exactly this: width fixed, height suddenly shorter), every hand slot's X
// target is unchanged but the whole row moves in Z (placement.localSeatZ). The
// registration effect used to key its "did this card move?" check on the X
// delta alone, so it never started a displacement tween and instead let
// applyIdlePoseIfNotInTransit write the new Z in one frame — a teleport.

// This mounts the real component, changes only the viewport height, and
// asserts a displacement tween is in flight with the card still at its old Z
// (i.e. it is transitioning, not snapping). A second case covers the related
// scale defect: ensureEntry is idempotent, so a card's draw-time pose.scale
// must be re-synced by the apply path whenever placement.handScale changes.

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
import type { ViewportInfo } from "$components/game/layout/seatLayout";
import type { GamePlayer } from "$stores/game.svelte";

const tallPortrait: ViewportInfo = { width: 390, height: 844, orientation: "portrait" };
const shortPortrait: ViewportInfo = { width: 390, height: 744, orientation: "portrait" };
const wideLandscape: ViewportInfo = { width: 1200, height: 800, orientation: "landscape" };
const shortLandscape: ViewportInfo = { width: 1200, height: 600, orientation: "landscape" };

const sevenCards: GamePlayer = {
	username: "me",
	card_count: 7,
	is_bot: false,
	hand: Array.from({ length: 7 }, (_, i) => ({
		id: i + 1,
		type: "red",
		value: String(i + 1)
	}))
} as unknown as GamePlayer;

function propsFor(viewport: ViewportInfo) {
	const geometry = computeSceneGeometry(viewport, 0);
	return {
		rig: geometry.rig,
		viewport,
		placement: geometry.placement,
		selectedId: null as number | null,
		onSelectionChange: vi.fn(),
		onPlay: vi.fn(),
		player: sevenCards
	};
}

describe("LocalHand3D abrupt resolution change", () => {
	beforeEach(() => {
		gsap.ticker.lagSmoothing(false);
	});

	afterEach(() => {
		cleanup();
		gsap.globalTimeline.clear();
	});

	it("tweens the hand in Z when only the viewport height changes (no X change)", async () => {
		const bus = new CardBus();
		const registry = new CardRegistry();
		const context = new Map<any, any>([
			[CARD_BUS_KEY, bus],
			[CARD_REGISTRY_KEY, registry]
		]);

		const tall = computeSceneGeometry(tallPortrait, 0).placement;
		const short = computeSceneGeometry(shortPortrait, 0).placement;
		// Premise: this really is a Z-only change for the hand row.
		expect(short.localSeatZ).not.toBeCloseTo(tall.localSeatZ, 1);

		const { rerender } = render(LocalHand3D, {
			props: propsFor(tallPortrait),
			context
		});

		await tick();
		await tick();

		const pose = registry.getPose("1");
		expect(pose).toBeDefined();
		const zBefore = pose!.z;
		expect(zBefore).toBeCloseTo(tall.localSeatZ);

		rerender(propsFor(shortPortrait));
		await tick();
		await tick();

		// The card must be mid-transition: a displacement tween is active and
		// the pose has NOT already snapped to the new seat Z.
		expect(gsap.getTweensOf(pose!).length).toBeGreaterThan(0);
		expect(registry.isInTransit("1")).toBe(true);
		expect(pose!.z).toBeCloseTo(zBefore);
		expect(pose!.z).not.toBeCloseTo(short.localSeatZ);
	});

	it("re-syncs pose.scale when the placement hand scale changes", async () => {
		const bus = new CardBus();
		const registry = new CardRegistry();
		const context = new Map<any, any>([
			[CARD_BUS_KEY, bus],
			[CARD_REGISTRY_KEY, registry]
		]);

		const wide = computeSceneGeometry(wideLandscape, 0).placement;
		const short = computeSceneGeometry(shortLandscape, 0).placement;
		// Premise: this resize really does change the hand scale.
		expect(short.handScale).not.toBeCloseTo(wide.handScale, 3);

		const { rerender } = render(LocalHand3D, {
			props: propsFor(wideLandscape),
			context
		});

		await tick();
		await tick();

		const pose = registry.getPose("1");
		expect(pose).toBeDefined();
		expect(pose!.scale).toBeCloseTo(wide.handScale);

		rerender(propsFor(shortLandscape));
		await tick();
		await tick();

		// ensureEntry is idempotent, so without an explicit re-sync the pose
		// would still carry the draw-time scale here.
		expect(pose!.scale).toBeCloseTo(short.handScale);
	});
});
