import { describe, it, expect, vi, afterEach } from "vitest";
import MockThrelte from "./MockThrelte.svelte";

vi.mock("@threlte/core", () => ({
	T: new Proxy(
		{},
		{
			get: () => MockThrelte
		}
	),
	useTask: vi.fn()
}));

function makeFakeTexture() {
	const texture: {
		repeat: { set: ReturnType<typeof vi.fn> };
		offset: { x: number };
		needsUpdate: boolean;
		clone: () => unknown;
	} = {
		repeat: { set: vi.fn() },
		offset: { x: 0 },
		needsUpdate: false,
		clone: () => makeFakeTexture()
	};
	return texture;
}

vi.mock("$components/game/three/textures", () => ({
	loadSilhouette: vi.fn(() => Promise.resolve(makeFakeTexture())),
	loadTexture: vi.fn(() => Promise.resolve(makeFakeTexture()))
}));

import { render, cleanup } from "@testing-library/svelte";
import PlayerSeat3D from "$components/game/three/PlayerSeat3D.svelte";
import { CardRegistry, CARD_REGISTRY_KEY } from "$components/game/animation/cardRegistry.svelte";
import { CardBus, CARD_BUS_KEY } from "$components/game/card-bus.svelte";
import {
	computeHandRingSlots,
	opponentRingRadiusWorld,
	ringSlotWorldPose,
	RING_RADIUS_EM
} from "$components/game/layout/handRing";
import { storeAnimation } from "$stores/animation.svelte";

describe("PlayerSeat3D CardRegistry integration", () => {
	afterEach(() => {
		cleanup();
	});

	it("registers opponent ring slots with correct world-space pose, spin, and scale", () => {
		const registry = new CardRegistry();
		const seat = { x: 3, z: -4, rotationY: 0.6 };
		const cardScale = 0.55;
		const avatarWorld = 0.78;

		render(PlayerSeat3D, {
			props: {
				player: { username: "alice", card_count: 3, is_bot: false },
				seat,
				color: "#00ff00",
				cardScale,
				avatarWorld
			},
			context: new Map([[CARD_REGISTRY_KEY, registry]])
		});

		const ringRadiusWorld = opponentRingRadiusWorld(avatarWorld, cardScale);
		const radialScale = ringRadiusWorld / RING_RADIUS_EM;
		const slots = computeHandRingSlots(3);
		const baseSpinDeg = (seat.rotationY * 180) / Math.PI + 180;

		expect(registry.activeFlights).toHaveLength(3);

		for (let i = 0; i < 3; i++) {
			const flight = registry.activeFlights.find((f) => f.id === `ring:alice:${i}`);
			expect(flight).toBeDefined();

			const expectedWorld = ringSlotWorldPose(seat, slots[i], i, radialScale, 0.02);
			expect(flight!.pose.x).toBeCloseTo(expectedWorld[0]);
			expect(flight!.pose.y).toBeCloseTo(expectedWorld[1]);
			expect(flight!.pose.z).toBeCloseTo(expectedWorld[2]);

			expect(flight!.pose.spinDeg).toBeCloseTo(baseSpinDeg + slots[i].rotateDeg);
			expect(flight!.pose.scale).toBe(cardScale);
			expect(flight!.pose.turned).toBe(true);
			expect(flight!.decoration?.dimmed).toBe(true);
		}
	});

	it("renders real faces for a hand the server sent (spectator view)", () => {
		const registry = new CardRegistry();
		const seat = { x: 0, z: 0, rotationY: 0 };
		const hand = [
			{ id: 11, type: "red", value: "5" },
			{ id: 12, type: "blue", value: "skip" }
		];

		render(PlayerSeat3D, {
			props: {
				player: { username: "bot_1", card_count: 2, is_bot: true, hand },
				seat,
				color: "#00ff00"
			},
			context: new Map([[CARD_REGISTRY_KEY, registry]])
		});

		expect(registry.activeFlights).toHaveLength(2);
		const first = registry.activeFlights.find((f) => f.id === "ring:bot_1:0")!;
		const second = registry.activeFlights.find((f) => f.id === "ring:bot_1:1")!;
		expect(first.pose.turned).toBe(false);
		expect(second.pose.turned).toBe(false);
		expect(first.card.value).toBe("5");
		expect(second.card.value).toBe("skip");
	});

	it("keeps the ring as backs when the server withheld the hand", () => {
		const registry = new CardRegistry();
		const seat = { x: 0, z: 0, rotationY: 0 };

		render(PlayerSeat3D, {
			props: {
				player: { username: "human_1", card_count: 2, is_bot: false },
				seat,
				color: "#00ff00"
			},
			context: new Map([[CARD_REGISTRY_KEY, registry]])
		});

		expect(registry.activeFlights).toHaveLength(2);
		expect(registry.activeFlights.every((f) => f.pose.turned === true)).toBe(true);
	});

	it("updates dimmed decoration when isTurn or isValidTarget is true", () => {
		const registry = new CardRegistry();
		const seat = { x: 0, z: 0, rotationY: 0 };

		const { rerender } = render(PlayerSeat3D, {
			props: {
				player: { username: "alice", card_count: 2, is_bot: false },
				seat,
				color: "#00ff00",
				isTurn: false,
				isValidTarget: false
			},
			context: new Map([[CARD_REGISTRY_KEY, registry]])
		});

		expect(registry.activeFlights[0].decoration?.dimmed).toBe(true);

		rerender({
			player: { username: "alice", card_count: 2, is_bot: false },
			seat,
			color: "#00ff00",
			isTurn: true,
			isValidTarget: false
		});

		expect(registry.activeFlights[0].decoration?.dimmed).toBe(false);

		rerender({
			player: { username: "alice", card_count: 2, is_bot: false },
			seat,
			color: "#00ff00",
			isTurn: false,
			isValidTarget: true
		});

		expect(registry.activeFlights[0].decoration?.dimmed).toBe(false);
	});

	it("cleans up removed slots when card count shrinks", () => {
		const registry = new CardRegistry();
		const seat = { x: 1, z: 1, rotationY: 0 };

		const { rerender } = render(PlayerSeat3D, {
			props: {
				player: { username: "bob", card_count: 4, is_bot: false },
				seat,
				color: "#0000ff"
			},
			context: new Map([[CARD_REGISTRY_KEY, registry]])
		});

		expect(registry.activeFlights).toHaveLength(4);
		expect(registry.activeFlights.map((f) => f.id)).toEqual([
			"ring:bob:0",
			"ring:bob:1",
			"ring:bob:2",
			"ring:bob:3"
		]);

		rerender({
			player: { username: "bob", card_count: 2, is_bot: false },
			seat,
			color: "#0000ff"
		});

		expect(registry.activeFlights).toHaveLength(2);
		expect(registry.activeFlights.map((f) => f.id)).toEqual(["ring:bob:0", "ring:bob:1"]);
	});

	it("subtracts inFlightDrawCount and hasHoldingCard from rendered ring slots", () => {
		const registry = new CardRegistry();
		const bus = new CardBus();
		bus.addInFlightDraw("charlie", 2);

		const seat = { x: 0, z: 0, rotationY: 0 };

		const { rerender } = render(PlayerSeat3D, {
			props: {
				player: { username: "charlie", card_count: 5, is_bot: false },
				seat,
				color: "#ffff00",
				hasHoldingCard: false
			},
			context: new Map<any, any>([
				[CARD_REGISTRY_KEY, registry],
				[CARD_BUS_KEY, bus]
			])
		});

		// 5 - 2 in flight = 3
		expect(registry.activeFlights).toHaveLength(3);

		// Now add hasHoldingCard = true: 5 - 2 - 1 = 2
		rerender({
			player: { username: "charlie", card_count: 5, is_bot: false },
			seat,
			color: "#ffff00",
			hasHoldingCard: true
		});

		expect(registry.activeFlights).toHaveLength(2);
	});

	it("cleans up all registered entries when component is unmounted", () => {
		const registry = new CardRegistry();
		const seat = { x: 0, z: 0, rotationY: 0 };

		render(PlayerSeat3D, {
			props: {
				player: { username: "dave", card_count: 3, is_bot: false },
				seat,
				color: "#ff00ff"
			},
			context: new Map([[CARD_REGISTRY_KEY, registry]])
		});

		expect(registry.activeFlights).toHaveLength(3);

		cleanup();

		expect(registry.activeFlights).toHaveLength(0);
	});

	it("provides pose provider that recomputes slot world pose", () => {
		const registry = new CardRegistry();
		const seat = { x: 2, z: 4, rotationY: 0 };

		render(PlayerSeat3D, {
			props: {
				player: { username: "eve", card_count: 1, is_bot: false },
				seat,
				color: "#00ffff"
			},
			context: new Map([[CARD_REGISTRY_KEY, registry]])
		});

		const flight = registry.activeFlights.find((f) => f.id === "ring:eve:0")!;
		expect(flight).toBeDefined();

		// Manually move pose away
		flight.pose.x = 999;
		flight.pose.y = 888;
		flight.pose.z = 777;

		// applyIdlePoseIfNotInTransit should restore via registered pose provider
		registry.applyIdlePoseIfNotInTransit("ring:eve:0");

		expect(flight.pose.x).not.toBe(999);
		expect(flight.pose.x).toBeCloseTo(2);
	});

	it("releases its ring cards from transit when the morph ends", () => {
		const registry = new CardRegistry();
		const seat = { x: 0, z: 0, rotationY: 0 };
		const player = { username: "alice", card_count: 2, is_bot: false };
		const morph = {
			username: "alice",
			poses: [
				[9, 0, 9],
				[8, 0, 8]
			] as [number, number, number][],
			open: false
		};

		const { rerender } = render(PlayerSeat3D, {
			props: { player, seat, color: "#00ff00", ringMorph: morph, inheritProgress: 0 },
			context: new Map([[CARD_REGISTRY_KEY, registry]])
		});

		expect(registry.isInTransit("ring:alice:0")).toBe(true);
		expect(registry.isInTransit("ring:alice:1")).toBe(true);

		// Animation off so the post-release idle re-sync clears the flag
		// synchronously rather than handing off to a displacement tween.
		const wasEnabled = storeAnimation.enabled;
		storeAnimation.enabled = false;
		rerender({ player, seat, color: "#00ff00", ringMorph: null, inheritProgress: 1 });
		storeAnimation.enabled = wasEnabled;

		expect(registry.isInTransit("ring:alice:0")).toBe(false);
		expect(registry.isInTransit("ring:alice:1")).toBe(false);
	});

	it("blends face → back and row spin → ring spin across the outgoing morph", () => {
		const registry = new CardRegistry();
		const seat = { x: 0, z: 0, rotationY: 0 };
		const player = { username: "alice", card_count: 2, is_bot: false };
		const morph = {
			username: "alice",
			poses: [
				[9, 0, 9],
				[8, 0, 8]
			] as [number, number, number][],
			open: false
		};
		const slots = computeHandRingSlots(2);
		const baseSpinDeg = (seat.rotationY * 180) / Math.PI + 180;

		const { rerender } = render(PlayerSeat3D, {
			props: { player, seat, color: "#00ff00", ringMorph: morph, inheritProgress: 0 },
			context: new Map([[CARD_REGISTRY_KEY, registry]])
		});

		// At the source it still reads as the hand-row card: face-up, row spin.
		expect(registry.getPose("ring:alice:0")!.flipDeg).toBeCloseTo(0);
		expect(registry.getPose("ring:alice:0")!.turned).toBe(false);
		expect(registry.getPose("ring:alice:0")!.spinDeg).toBeCloseTo(0);

		rerender({ player, seat, color: "#00ff00", ringMorph: morph, inheritProgress: 1 });

		// At the target it has turned fully onto its back and taken the ring
		// slot's own in-plane orientation.
		expect(registry.getPose("ring:alice:0")!.flipDeg).toBeCloseTo(180);
		expect(registry.getPose("ring:alice:0")!.turned).toBe(false);
		expect(registry.getPose("ring:alice:0")!.spinDeg).toBeCloseTo(baseSpinDeg + slots[0].rotateDeg);
	});

	it("registers no ring cards and retires existing ones when ringCardsHidden", () => {
		const registry = new CardRegistry();
		const seat = { x: 0, z: 0, rotationY: 0 };
		const player = { username: "frank", card_count: 3, is_bot: false };

		const { rerender } = render(PlayerSeat3D, {
			props: { player, seat, color: "#ffffff" },
			context: new Map([[CARD_REGISTRY_KEY, registry]])
		});
		expect(registry.activeFlights).toHaveLength(3);

		rerender({ player, seat, color: "#ffffff", ringCardsHidden: true });
		expect(registry.activeFlights).toHaveLength(0);

		rerender({ player, seat, color: "#ffffff", ringCardsHidden: false });
		expect(registry.activeFlights).toHaveLength(3);
	});

	it("renders avatar and name label as scene objects with no DOM overlay", () => {
		const registry = new CardRegistry();
		const seat = { x: 0, z: 0, rotationY: 0 };

		const { container } = render(PlayerSeat3D, {
			props: {
				player: { username: "star_player", card_count: 2, is_bot: false },
				seat,
				color: "#00ffcc",
				isTurn: true
			},
			context: new Map([[CARD_REGISTRY_KEY, registry]])
		});

		// The old implementation emitted an HTML overlay (<button>, <svg>,
		// <textPath>) that fell outside WebGL render order. The native-sprite
		// version must emit none of it.
		expect(container.querySelector(".avatar-box")).toBeNull();
		expect(container.querySelector(".seat-arc-container")).toBeNull();
		expect(container.querySelector(".seat-arc-text")).toBeNull();
		expect(container.querySelector("svg")).toBeNull();
		expect(container.querySelector("textPath")).toBeNull();
	});
});
