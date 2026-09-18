import { describe, it, expect, vi, afterEach } from "vitest";
import MockThrelte from "./MockThrelte.svelte";

vi.mock("@threlte/core", () => ({
	T: new Proxy(
		{},
		{
			get: () => MockThrelte
		}
	)
}));

vi.mock("@threlte/extras", () => ({
	HTML: MockThrelte
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

	it("renders player name on an SVG curved arc and updates active state on turn", () => {
		const registry = new CardRegistry();
		const seat = { x: 0, z: 0, rotationY: 0 };

		const { container, rerender } = render(PlayerSeat3D, {
			props: {
				player: { username: "star_player", card_count: 2, is_bot: false },
				seat,
				color: "#00ffcc",
				isTurn: false,
				arcMode: "overhead"
			},
			context: new Map([[CARD_REGISTRY_KEY, registry]])
		});

		const labelContainer = container.querySelector(".seat-arc-container");
		expect(labelContainer).toBeInTheDocument();
		expect(labelContainer).not.toHaveClass("is-shown");
		expect(labelContainer).not.toHaveClass("is-active");

		const textPath = container.querySelector("textPath");
		expect(textPath).toBeInTheDocument();
		expect(textPath?.textContent?.trim()).toBe("star_player");

		const rail = container.querySelector(".seat-arc-rail");
		expect(rail).toBeInTheDocument();

		rerender({
			player: { username: "star_player", card_count: 2, is_bot: false },
			seat,
			color: "#00ffcc",
			isTurn: true,
			arcMode: "overhead"
		});

		expect(labelContainer).toHaveClass("is-shown");
		expect(labelContainer).toHaveClass("is-active");
	});

	it("updates arc path and viewBox when switching arcMode", () => {
		const registry = new CardRegistry();
		const seat = { x: 0, z: 0, rotationY: 0 };

		const { container, rerender } = render(PlayerSeat3D, {
			props: {
				player: { username: "arc_tester", card_count: 1, is_bot: false },
				seat,
				color: "#ff0088",
				arcMode: "overhead"
			},
			context: new Map([[CARD_REGISTRY_KEY, registry]])
		});

		const svg = container.querySelector(".seat-arc-svg");
		expect(svg?.getAttribute("viewBox")).toBe("-100 -70 200 140");

		rerender({
			player: { username: "arc_tester", card_count: 1, is_bot: false },
			seat,
			color: "#ff0088",
			arcMode: "cards-outer"
		});

		expect(svg?.getAttribute("viewBox")).toBe("-90 -50 180 60");

		rerender({
			player: { username: "arc_tester", card_count: 1, is_bot: false },
			seat,
			color: "#ff0088",
			arcMode: "cards-inner"
		});

		expect(svg?.getAttribute("viewBox")).toBe("-70 -40 140 50");
	});

	it("scales font size dynamically for long opponent names so they do not get cut off", () => {
		const registry = new CardRegistry();
		const seat = { x: 0, z: 0, rotationY: 0 };

		// Short name (3 chars) gets base font size 21 (1.15 * 18 = 21)
		const { container, rerender } = render(PlayerSeat3D, {
			props: {
				player: { username: "bob", card_count: 1, is_bot: false },
				seat,
				color: "#00ffcc",
				arcMode: "overhead"
			},
			context: new Map([[CARD_REGISTRY_KEY, registry]])
		});

		const textElem = container.querySelector(".seat-arc-text");
		expect(textElem).toBeInTheDocument();
		expect(Number(textElem?.getAttribute("font-size"))).toBe(21);
		expect(Number(textElem?.getAttribute("dy"))).toBe(-9);

		// Long name (12 chars: scriptxcorso) scales font size down to fit within arc
		rerender({
			player: { username: "scriptxcorso", card_count: 1, is_bot: false },
			seat,
			color: "#00ffcc",
			arcMode: "overhead"
		});

		const scriptxFontSize = Number(textElem?.getAttribute("font-size"));
		expect(scriptxFontSize).toBeLessThan(21);
		expect(scriptxFontSize).toBeGreaterThanOrEqual(14);
		expect(Number(textElem?.getAttribute("dy"))).toBeLessThanOrEqual(-6);

		// 15-character name scales down further
		rerender({
			player: { username: "superlongplayer", card_count: 1, is_bot: false },
			seat,
			color: "#00ffcc",
			arcMode: "overhead"
		});

		const longFontSize = Number(textElem?.getAttribute("font-size"));
		expect(longFontSize).toBeLessThan(scriptxFontSize);
		expect(longFontSize).toBeGreaterThanOrEqual(10);
	});

	it("truncates names longer than 16 chars with ellipsis and applies text compression fallback if needed", () => {
		const registry = new CardRegistry();
		const seat = { x: 0, z: 0, rotationY: 0 };

		const { container } = render(PlayerSeat3D, {
			props: {
				player: { username: "a_very_extremely_long_name_indeed", card_count: 1, is_bot: false },
				seat,
				color: "#00ffcc",
				arcMode: "overhead"
			},
			context: new Map([[CARD_REGISTRY_KEY, registry]])
		});

		const textPath = container.querySelector("textPath");
		expect(textPath?.textContent?.trim()).toBe("a_very_extremel…");
	});
});
