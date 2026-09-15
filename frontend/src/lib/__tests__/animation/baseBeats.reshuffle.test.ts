import { describe, it, expect } from "vitest";
import {
	detectReshuffle,
	buildReshuffleBeat,
	reshuffleStaggerFor
} from "$components/game/animation/baseBeats.svelte";
import { CardRegistry } from "$components/game/animation/cardRegistry.svelte";
import type { DiscardEntry } from "$components/game/layout/discardPile";

describe("detectReshuffle", () => {
	it("is true when draw_pile_size jumps upward between two states", () => {
		expect(detectReshuffle(0, 60)).toBe(true);
	});

	it("is false when draw_pile_size only ever decreases or holds", () => {
		expect(detectReshuffle(10, 9)).toBe(false);
		expect(detectReshuffle(10, 10)).toBe(false);
	});
});

describe("reshuffleStaggerFor", () => {
	it("bounds total reshuffle stagger to 1.2s", () => {
		const count = 30;
		const stagger = reshuffleStaggerFor(count);
		expect((count - 1) * stagger).toBeLessThanOrEqual(1.2001);
	});

	it("caps per-card stagger at 0.06s for small counts", () => {
		expect(reshuffleStaggerFor(5)).toBe(0.06);
		expect(reshuffleStaggerFor(2)).toBe(0.06);
	});

	it("handles counts <= 1 gracefully", () => {
		expect(reshuffleStaggerFor(1)).toBe(0.06);
		expect(reshuffleStaggerFor(0)).toBe(0.06);
	});
});

describe("buildReshuffleBeat", () => {
	it("moves every discard entry except the top one back onto the draw pile, in one beat with staggered atS", () => {
		const history: DiscardEntry[] = [
			{ card: { id: 1, type: "red", value: "1" }, seq: 1, jitter: [0, 0], rotationDeg: 0 },
			{ card: { id: 2, type: "blue", value: "2" }, seq: 2, jitter: [0, 0], rotationDeg: 0 },
			{ card: { id: 3, type: "green", value: "3" }, seq: 3, jitter: [0, 0], rotationDeg: 0 }
		];

		const beats = buildReshuffleBeat(history);
		expect(beats).toHaveLength(1);
		const [beat] = beats;
		expect(beat).toHaveLength(2);
		expect(beat.every((s) => s.op === "move" && s.payload?.to === "draw-pile")).toBe(true);
		expect(beat.map((s) => s.target)).toEqual(["1", "2"]);

		const stagger = reshuffleStaggerFor(2);
		expect(beat[0].atS).toBe(0);
		expect(beat[1].atS).toBe(stagger);
	});

	it("assigns sequential atS offsets bounded by reshuffleStaggerFor on large piles", () => {
		const count = 25;
		const history: DiscardEntry[] = Array.from({ length: count }, (_, i) => ({
			card: { id: i + 1, type: "red", value: String(i) },
			seq: i + 1,
			jitter: [0, 0] as [number, number],
			rotationDeg: 0
		}));

		const beats = buildReshuffleBeat(history);
		expect(beats).toHaveLength(1);
		const [beat] = beats;
		const toReshuffleCount = count - 1;
		expect(beat).toHaveLength(toReshuffleCount);
		const stagger = reshuffleStaggerFor(toReshuffleCount);
		beat.forEach((step, i) => {
			expect(step.atS).toBeCloseTo(i * stagger, 5);
		});
	});

	it("leaves empty beats when history has 1 or fewer cards", () => {
		expect(buildReshuffleBeat([])).toEqual([]);
		expect(
			buildReshuffleBeat([
				{ card: { id: 1, type: "red", value: "1" }, seq: 1, jitter: [0, 0], rotationDeg: 0 }
			])
		).toEqual([]);
	});
});

describe("CardBus.retainTopDiscard", () => {
	it("retains strictly the top card on reshuffle", async () => {
		const { CardBus } = await import("$components/game/card-bus.svelte");
		const bus = new CardBus();
		bus.discardHistory = [
			{ card: { id: 1, type: "red", value: "1" }, seq: 1, jitter: [0, 0], rotationDeg: 0 },
			{ card: { id: 2, type: "blue", value: "2" }, seq: 2, jitter: [0, 0], rotationDeg: 0 },
			{ card: { id: 3, type: "green", value: "3" }, seq: 3, jitter: [0, 0], rotationDeg: 0 }
		];
		bus.retainTopDiscard(2);
		expect(bus.discardHistory).toHaveLength(1);
		expect(bus.discardHistory[0].card.id).toBe(3);
	});

	it("keeps a card played while the reshuffle flight was still in the air", async () => {
		const { CardBus } = await import("$components/game/card-bus.svelte");
		const bus = new CardBus();
		bus.discardHistory = [
			{ card: { id: 1, type: "red", value: "1" }, seq: 1, jitter: [0, 0], rotationDeg: 0 },
			{ card: { id: 2, type: "blue", value: "2" }, seq: 2, jitter: [0, 0], rotationDeg: 0 },
			{ card: { id: 3, type: "green", value: "3" }, seq: 3, jitter: [0, 0], rotationDeg: 0 }
		];
		const reshuffledCount = bus.discardHistory.length - 1;
		// A play lands mid-flight, appending a new top card before retainTopDiscard runs.
		bus.discardHistory = [
			...bus.discardHistory,
			{ card: { id: 4, type: "green", value: "4" }, seq: 4, jitter: [0, 0], rotationDeg: 0 }
		];
		bus.retainTopDiscard(reshuffledCount);
		expect(bus.discardHistory.map((e) => e.card.id)).toEqual([3, 4]);
	});
});

describe("CardRegistry.isInTransit", () => {
	it("tracks in-transit state during beat execution", async () => {
		const registry = new CardRegistry();
		expect(registry.isInTransit("c1")).toBe(false);

		const done = registry.enqueue(
			[[{ op: "move", target: "c1", payload: { to: "draw-pile" } }]],
			() => [0, 0, 0]
		);
		expect(registry.isInTransit("c1")).toBe(true);

		registry.skipCurrent();
		await done;

		expect(registry.isInTransit("c1")).toBe(false);
	});
});
