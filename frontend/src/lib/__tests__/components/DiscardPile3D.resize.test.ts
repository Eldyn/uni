// Regression test for already-played discard cards not reacting to a viewport
// / resolution change.

// DiscardPile3D registers each pile card through CardRegistry.ensureEntry,
// which is idempotent — so unless its registration effect re-syncs pose.scale
// on every layout pass, a card created before a resize keeps the scale it was
// drawn at forever. That is exactly the reported bug: switch resolution and
// back, and the single cards already on the discard pile read as oversized
// next to the rest of the board. This mirrors LocalHand3D.resize.test.ts's
// scale case for the pile that had been missed.

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
import DiscardPile3D from "$components/game/three/DiscardPile3D.svelte";
import { computeSceneGeometry } from "$components/game/layout/sceneGeometry";
import { CardRegistry, CARD_REGISTRY_KEY } from "$components/game/animation/cardRegistry.svelte";
import { discardEntryFor } from "$components/game/layout/discardPile";
import { storeMatchIntro } from "$stores/matchIntro.svelte";
import type { ViewportInfo } from "$components/game/layout/seatLayout";
import type { Card } from "$stores/game.svelte";

const wide: ViewportInfo = { width: 1920, height: 1080, orientation: "landscape" };
const short: ViewportInfo = { width: 1200, height: 600, orientation: "landscape" };

const history = [
	discardEntryFor({ id: 11, type: "red", value: "3" } as Card, 0),
	discardEntryFor({ id: 12, type: "blue", value: "4" } as Card, 1)
];

function propsFor(viewport: ViewportInfo) {
	const geometry = computeSceneGeometry(viewport, 9);
	return {
		history,
		placement: geometry.placement,
		armed: false,
		onConfirm: vi.fn()
	};
}

describe("DiscardPile3D resolution change", () => {
	beforeEach(() => {
		gsap.ticker.lagSmoothing(false);
	});

	afterEach(() => {
		cleanup();
		gsap.globalTimeline.clear();
		storeMatchIntro.end();
	});

	it("re-syncs pose.scale of already-played cards when the placement changes", async () => {
		const registry = new CardRegistry();
		const context = new Map<any, any>([[CARD_REGISTRY_KEY, registry]]);

		const widePlacement = computeSceneGeometry(wide, 9).placement;
		const shortPlacement = computeSceneGeometry(short, 9).placement;
		// Premise: this resize really does change the pile's card scale.
		expect(shortPlacement.centerScale).not.toBeCloseTo(widePlacement.centerScale, 3);

		const { rerender } = render(DiscardPile3D, {
			props: propsFor(wide),
			context
		});

		await tick();
		await tick();

		const pose = registry.getPose("11");
		expect(pose).toBeDefined();
		expect(pose!.scale).toBeCloseTo(widePlacement.centerScale);

		rerender(propsFor(short));
		await tick();
		await tick();

		// ensureEntry is idempotent, so without an explicit re-sync the pose
		// would still carry its draw-time scale here.
		expect(pose!.scale).toBeCloseTo(shortPlacement.centerScale);
	});

	it("plants no registry entries while the intro hides the pile, then resumes", async () => {
		const registry = new CardRegistry();
		const context = new Map<any, any>([[CARD_REGISTRY_KEY, registry]]);

		storeMatchIntro.discardHidden = true;
		render(DiscardPile3D, { props: propsFor(wide), context });
		await tick();
		await tick();

		// The registry is how discard cards actually get rendered, so "renders
		// nothing" means no entry is planted for any history card.
		expect(registry.getPose("11")).toBeUndefined();
		expect(registry.getPose("12")).toBeUndefined();

		storeMatchIntro.discardHidden = false;
		await tick();
		await tick();

		expect(registry.getPose("11")).toBeDefined();
		expect(registry.getPose("12")).toBeDefined();
	});

	it("removes already-planted entries when the intro hides the pile mid-life", async () => {
		const registry = new CardRegistry();
		const context = new Map<any, any>([[CARD_REGISTRY_KEY, registry]]);

		render(DiscardPile3D, { props: propsFor(wide), context });
		await tick();
		await tick();
		expect(registry.getPose("11")).toBeDefined();
		expect(registry.getPose("12")).toBeDefined();

		storeMatchIntro.discardHidden = true;
		await tick();
		await tick();
		expect(registry.getPose("11")).toBeUndefined();
		expect(registry.getPose("12")).toBeUndefined();
	});
});
