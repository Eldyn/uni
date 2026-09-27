import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import MockMesh from "./MockMesh.svelte";
import MockCardMesh from "./MockCardMesh.svelte";
import { meshInstances, cardMeshInstances, resetMockState } from "./drawPileMockState";

vi.mock("@threlte/core", () => ({
	T: new Proxy(
		{},
		{
			get: (_, prop) => {
				if (prop === "Mesh") {
					return MockMesh;
				}
				return MockMesh; // PlaneGeometry, MeshBasicMaterial, etc. pass through
			}
		}
	)
}));

vi.mock("$components/game/three/CardMesh3D.svelte", () => ({
	default: MockCardMesh
}));

vi.mock("./textures", () => ({
	loadSilhouette: vi.fn().mockResolvedValue(null)
}));

import { render, cleanup } from "@testing-library/svelte";
import DrawPile3D from "$components/game/three/DrawPile3D.svelte";
import { CardRegistry, CARD_REGISTRY_KEY } from "$components/game/animation/cardRegistry.svelte";
import { CardBus, CARD_BUS_KEY } from "$components/game/card-bus.svelte";
import { storeGame } from "$stores/game.svelte";
import { storeAuth } from "$stores/auth.svelte";
import { storeRenderSettings } from "$stores/renderSettings.svelte";
import { storeAnimation } from "$stores/animation.svelte";
import { storeMatchIntro } from "$stores/matchIntro.svelte";
import { PILE_BASE_HEIGHT, PILE_PEEK_Z } from "$components/game/layout/drawPile";
import { CARD_WIDTH, CARD_HEIGHT } from "$components/game/three/units";
import type { BoardPlacement } from "$components/game/layout/boardPlacement";

describe("DrawPile3D", () => {
	const defaultPlacement: BoardPlacement = {
		mat: {
			size: [10, 10],
			offsetZ: 0,
			bounds: { left: -5, right: 5, far: -5, near: 5 }
		},
		handScale: 1,
		centerScale: 1,
		discardX: 0,
		discardZ: 0,
		localSeatZ: 1.5,
		localAvatarZ: 1.2,
		drawPileX: -2.5,
		drawPileZ: 0.8,
		drawPileScale: 0.9
	};

	beforeEach(() => {
		resetMockState();
		storeAuth.username = "alice";
		storeRenderSettings.drawPileThickness = "capped";
		storeGame.state = {
			active_type: "red",
			current_turn: "alice",
			play_direction: 1,
			players: [
				{ username: "alice", card_count: 5, is_bot: false },
				{ username: "bob", card_count: 5, is_bot: false }
			],
			pending_draws: 0,
			draw_pile_size: 20
		};
		storeGame.isActionPending = false;
	});

	afterEach(() => {
		cleanup();
		storeGame.state = null;
		storeAnimation.enabled = true;
		storeAnimation.speedMultiplier = 1;
		storeMatchIntro.end();
	});

	it("renders an invisible hitbox spanning the whole pile and removes onclick from card meshes", () => {
		const registry = new CardRegistry();

		render(DrawPile3D, {
			props: { placement: defaultPlacement },
			context: new Map([[CARD_REGISTRY_KEY, registry]])
		});

		// 6 cards rendered in capped mode with pile size 20
		expect(cardMeshInstances.length).toBe(6);
		// Card meshes should NOT have individual onclick handlers
		for (const cardMesh of cardMeshInstances) {
			expect(cardMesh.onclick).toBeUndefined();
		}

		// Hitbox mesh should be present with onclick handler
		const hitbox = meshInstances.find(
			(m) => typeof m.onclick === "function" && Array.isArray(m.position)
		);
		expect(hitbox).toBeDefined();

		// The hitbox is a box, not a flat plane on the top card: it centres on
		// the stack's mid-height and spans the card depth PLUS the peek run the
		// deeper backs step out along -Z, so the whole pile is tappable.
		const renderedCount = 6;
		const stackHeight = (renderedCount - 1) * 0.02;
		const peekRun = (renderedCount - 1) * PILE_PEEK_Z * defaultPlacement.drawPileScale;

		expect(hitbox!.position![0]).toBeCloseTo(defaultPlacement.drawPileX);
		expect(hitbox!.position![1]).toBeCloseTo(PILE_BASE_HEIGHT + stackHeight / 2);
		expect(hitbox!.position![2]).toBeCloseTo(defaultPlacement.drawPileZ - peekRun / 2);

		const box = meshInstances.find((m) => Array.isArray(m.args) && m.args.length === 3);
		expect(box).toBeDefined();
		const [boxW, boxH, boxD] = box!.args as number[];
		expect(boxW).toBeCloseTo(CARD_WIDTH * defaultPlacement.drawPileScale);
		expect(boxH).toBeCloseTo(stackHeight);
		expect(boxD).toBeCloseTo(CARD_HEIGHT * defaultPlacement.drawPileScale + peekRun);

		// Trigger click
		const drawSpy = vi.spyOn(storeGame, "drawCard").mockImplementation(() => {});
		hitbox!.onclick!({});
		expect(drawSpy).toHaveBeenCalledTimes(1);

		// If not alice's turn, click does not draw
		storeGame.state!.current_turn = "bob";
		hitbox!.onclick!({});
		expect(drawSpy).toHaveBeenCalledTimes(1);
	});

	it("registers synthetic pose providers pile:draw:i in CardRegistry and cleans up on unmount", () => {
		const registry = new CardRegistry();

		const { unmount } = render(DrawPile3D, {
			props: { placement: defaultPlacement },
			context: new Map([[CARD_REGISTRY_KEY, registry]])
		});

		// Capped mode with size 20 -> 6 rendered cards
		for (let i = 0; i < 6; i++) {
			const id = `pile:draw:${i}`;
			const seed = registry.seedPose(id, {
				x: 0,
				y: 0,
				z: 0,
				spinDeg: 0,
				flipDeg: 0,
				scale: 1,
				turned: false,
				opacity: 1
			});
			registry.applyIdlePoseIfNotInTransit(id);

			expect(seed.x).toBeCloseTo(defaultPlacement.drawPileX);
			expect(seed.y).toBeCloseTo(PILE_BASE_HEIGHT + i * 0.02);
			expect(seed.z).toBeCloseTo(
				defaultPlacement.drawPileZ - i * PILE_PEEK_Z * defaultPlacement.drawPileScale
			);
		}

		// On unmount, all synthetic entries are cleaned up
		unmount();
		const unmountedSeed = registry.seedPose("pile:draw:0", {
			x: 99,
			y: 99,
			z: 99,
			spinDeg: 0,
			flipDeg: 0,
			scale: 1,
			turned: false,
			opacity: 1
		});
		registry.applyIdlePoseIfNotInTransit("pile:draw:0");
		expect(unmountedSeed.x).toBe(99); // Provider was removed
	});

	it("cleans up removed keys when rendered count decreases", async () => {
		const registry = new CardRegistry();

		render(DrawPile3D, {
			props: { placement: defaultPlacement },
			context: new Map([[CARD_REGISTRY_KEY, registry]])
		});

		// Initially 6 cards (0..5)
		const seed5 = registry.seedPose("pile:draw:5", {
			x: 0,
			y: 0,
			z: 0,
			spinDeg: 0,
			flipDeg: 0,
			scale: 1,
			turned: false,
			opacity: 1
		});
		registry.applyIdlePoseIfNotInTransit("pile:draw:5");
		expect(seed5.x).toBeCloseTo(defaultPlacement.drawPileX);

		// Decrease pile size to 3 cards in full mode (count = 3)
		storeRenderSettings.drawPileThickness = "full";
		storeGame.state!.draw_pile_size = 3;

		// Allow reactive effects to run
		await new Promise((r) => setTimeout(r, 10));

		// pile:draw:5 should now have its provider cleared
		seed5.x = 88;
		registry.applyIdlePoseIfNotInTransit("pile:draw:5");
		expect(seed5.x).toBe(88); // Provider no longer active

		// pile:draw:0..2 should still be registered
		const seed0 = registry.seedPose("pile:draw:0", {
			x: 0,
			y: 0,
			z: 0,
			spinDeg: 0,
			flipDeg: 0,
			scale: 1,
			turned: false,
			opacity: 1
		});
		registry.applyIdlePoseIfNotInTransit("pile:draw:0");
		expect(seed0.x).toBeCloseTo(defaultPlacement.drawPileX);
	});

	it("overrides rendered count with bus.reshuffleDrawPileSize and updates dynamically", async () => {
		const registry = new CardRegistry();
		const bus = new CardBus();

		storeRenderSettings.drawPileThickness = "full";
		storeGame.state!.draw_pile_size = 30; // State says 30
		bus.reshuffleDrawPileSize = 2; // Reshuffle starts with 2 cards arriving

		render(DrawPile3D, {
			props: { placement: defaultPlacement },
			context: new Map<any, any>([
				[CARD_REGISTRY_KEY, registry],
				[CARD_BUS_KEY, bus]
			])
		});

		// Should render 2 cards based on reshuffleDrawPileSize, not 30
		expect(cardMeshInstances.length).toBe(2);

		// Another card arrives (+1)
		bus.reshuffleDrawPileSize = 3;
		await new Promise((r) => setTimeout(r, 10));
		expect(cardMeshInstances.length).toBe(3);

		// Reshuffle completes, reset to null
		bus.reshuffleDrawPileSize = null;
		await new Promise((r) => setTimeout(r, 10));
		expect(cardMeshInstances.length).toBe(30);
	});

	it("keeps a tappable placeholder pile when the draw pile is empty but a reshuffle is available", () => {
		const registry = new CardRegistry();

		storeGame.state!.draw_pile_size = 0;
		storeGame.state!.discard_pile_size = 12;

		render(DrawPile3D, {
			props: { placement: defaultPlacement },
			context: new Map([[CARD_REGISTRY_KEY, registry]])
		});

		// A single dimmed back stands in for the exhausted draw pile so there is
		// something to tap to trigger the reshuffle.
		expect(cardMeshInstances.length).toBe(1);

		const hitbox = meshInstances.find(
			(m) => typeof m.onclick === "function" && Array.isArray(m.position)
		);
		expect(hitbox).toBeDefined();

		const drawSpy = vi.spyOn(storeGame, "drawCard").mockImplementation(() => {});
		drawSpy.mockClear();
		hitbox!.onclick!({});
		expect(drawSpy).toHaveBeenCalledTimes(1);
	});

	it("hides the pile when neither the draw pile nor the discard pile can produce a draw", () => {
		const registry = new CardRegistry();

		storeGame.state!.draw_pile_size = 0;
		storeGame.state!.discard_pile_size = 1;

		render(DrawPile3D, {
			props: { placement: defaultPlacement },
			context: new Map([[CARD_REGISTRY_KEY, registry]])
		});

		expect(cardMeshInstances.length).toBe(0);
		const hitbox = meshInstances.find(
			(m) => typeof m.onclick === "function" && Array.isArray(m.position)
		);
		expect(hitbox).toBeUndefined();
	});

	it("uses storeMatchIntro.drawPilePos for the rendered pile and pose providers", () => {
		const registry = new CardRegistry();
		storeMatchIntro.drawPilePos = { x: 4.2, z: -1.3 };

		render(DrawPile3D, {
			props: { placement: defaultPlacement },
			context: new Map([[CARD_REGISTRY_KEY, registry]])
		});

		const hitbox = meshInstances.find(
			(m) => typeof m.onclick === "function" && Array.isArray(m.position)
		);
		expect(hitbox).toBeDefined();
		const peekRun = (6 - 1) * PILE_PEEK_Z * defaultPlacement.drawPileScale;
		expect(hitbox!.position![0]).toBeCloseTo(4.2);
		expect(hitbox!.position![2]).toBeCloseTo(-1.3 - peekRun / 2);

		const seed = registry.seedPose("pile:draw:0", {
			x: 0,
			y: 0,
			z: 0,
			spinDeg: 0,
			flipDeg: 0,
			scale: 1,
			turned: false,
			opacity: 1
		});
		registry.applyIdlePoseIfNotInTransit("pile:draw:0");
		expect(seed.x).toBeCloseTo(4.2);
		expect(seed.z).toBeCloseTo(-1.3);
	});

	it("uses storeMatchIntro.drawPileCount over the real draw-pile size", () => {
		const registry = new CardRegistry();
		storeRenderSettings.drawPileThickness = "full";
		storeGame.state!.draw_pile_size = 30;
		storeMatchIntro.drawPileCount = 3;

		render(DrawPile3D, {
			props: { placement: defaultPlacement },
			context: new Map([[CARD_REGISTRY_KEY, registry]])
		});

		expect(cardMeshInstances.length).toBe(3);
	});

	it("withholds the draw hover dip while the intro is active (canDraw false)", async () => {
		storeAnimation.enabled = true;
		storeAnimation.speedMultiplier = 3;
		storeMatchIntro.drawPileCount = 2;
		const bus = new CardBus();
		const context = new Map<any, any>([[CARD_BUS_KEY, bus]]);

		render(DrawPile3D, { props: { placement: defaultPlacement }, context });
		let hitbox = meshInstances.find((m) => typeof m.onpointerenter === "function")!;
		hitbox.onpointerenter!({});
		await new Promise((r) => setTimeout(r, 250));
		const hoveredDip = bus.getDrawPileHoverDipZ();

		cleanup();
		resetMockState();

		storeMatchIntro.active = true;
		render(DrawPile3D, { props: { placement: defaultPlacement }, context });
		hitbox = meshInstances.find((m) => typeof m.onpointerenter === "function")!;
		hitbox.onpointerenter!({});
		await new Promise((r) => setTimeout(r, 250));
		const hoveredNoDip = bus.getDrawPileHoverDipZ();

		// The dip settles at HOVER_DIP_Z (0.14) forward on the top card, and
		// canDraw gates it off entirely while the intro is running.
		expect(hoveredDip).toBeCloseTo(0.14);
		expect(hoveredNoDip).toBe(0);
	});
});
