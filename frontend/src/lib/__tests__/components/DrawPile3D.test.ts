import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import MockMesh from "./MockMesh.svelte";
import MockCardMesh from "./MockCardMesh.svelte";
import { meshInstances, cardMeshInstances, resetMockState } from "./drawPileMockState";

vi.mock("@threlte/core", () => ({
	T: new Proxy({}, {
		get: (_, prop) => {
			if (prop === "Mesh") {
				return MockMesh;
			}
			return MockMesh; // PlaneGeometry, MeshBasicMaterial, etc. pass through
		}
	})
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
import { storeGame } from "$stores/game.svelte";
import { storeAuth } from "$stores/auth.svelte";
import { storeRenderSettings } from "$stores/renderSettings.svelte";
import { PILE_BASE_HEIGHT, PILE_PEEK_Z } from "$components/game/layout/drawPile";
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
		drawPileScale: 0.9,
		drawPileBesideHand: true
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
	});

	it("renders invisible full-pile hitbox at visual top and removes onclick from card meshes", () => {
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

		const expectedTopY = PILE_BASE_HEIGHT + (6 - 1) * 0.02 + 0.005;
		const expectedTopZ =
			defaultPlacement.drawPileZ - (6 - 1) * PILE_PEEK_Z * defaultPlacement.drawPileScale;

		expect(hitbox!.position![0]).toBeCloseTo(defaultPlacement.drawPileX);
		expect(hitbox!.position![1]).toBeCloseTo(expectedTopY);
		expect(hitbox!.position![2]).toBeCloseTo(expectedTopZ);
		expect(hitbox!["rotation.x"]).toBeCloseTo(-Math.PI / 2);
		expect(hitbox!.scale).toBe(defaultPlacement.drawPileScale);

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
});
