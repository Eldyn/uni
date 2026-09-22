import { describe, it, expect } from "vitest";
import { flushSync } from "svelte";
import { CardBus } from "$components/game/card-bus.svelte";

// Regression test for the "entering the game screen crashes the whole page"
// bug: LocalHand3D's own $effect calls bus.setLocalHandSnapshot(...) every
// time its hand layout recomputes. setLocalHandSnapshot used to read
// this.localHandSnapshot (a $state field) to capture the previous value —
// that read gets tracked as a dependency of whoever's effect called it. Since
// the same call always writes a brand-new object to that same field, the
// calling effect's own write became its own trigger: read -> write new value
// -> dependency changed -> effect reruns -> read -> write again -> forever,
// hitting Svelte's effect_update_depth_exceeded guard.
describe("CardBus.setLocalHandSnapshot", () => {
	it("does not re-trigger an effect that calls it, even though it writes localHandSnapshot on every call", () => {
		const bus = new CardBus();
		let runs = 0;

		const dispose = $effect.root(() => {
			$effect(() => {
				runs++;
				// Mirrors LocalHand3D.svelte's own snapshot-broadcast effect: an
				// unconditional write, no equality guard, every run.
				bus.setLocalHandSnapshot({ orderIds: [1, 2, 3], scrollEm: 0, maxHalfSpanEm: 10 });
			});
		});

		flushSync();

		expect(runs).toBe(1);
		dispose();
	});
});

describe("CardBus draw-pile hover slide", () => {
	it("defaults to 0 and round-trips a set value", () => {
		const bus = new CardBus();
		expect(bus.getDrawPileHoverDipZ()).toBe(0);

		bus.setDrawPileHoverDipZ(0.06);
		expect(bus.getDrawPileHoverDipZ()).toBe(0.06);
	});

	it("does not re-run an effect that reads it (non-reactive by design)", () => {
		const bus = new CardBus();
		let runs = 0;

		const dispose = $effect.root(() => {
			$effect(() => {
				runs++;
				bus.getDrawPileHoverDipZ();
			});
		});

		flushSync();
		expect(runs).toBe(1);

		// DrawPile3D writes this every GSAP frame; if it were $state the
		// baseBeats watcher (also an $effect) would re-run on each frame.
		bus.setDrawPileHoverDipZ(0.06);
		flushSync();
		expect(runs).toBe(1);

		dispose();
	});
});

describe("CardBus in-flight counters", () => {
	it("increments and decrements in-flight draw count per user", () => {
		const bus = new CardBus();
		expect(bus.getInFlightDrawCount("player1")).toBe(0);

		const cleanup1 = bus.addInFlightDraw("player1");
		expect(bus.getInFlightDrawCount("player1")).toBe(1);
		expect(bus.getInFlightDrawCount("player2")).toBe(0);

		const cleanup2 = bus.addInFlightDraw("player1");
		expect(bus.getInFlightDrawCount("player1")).toBe(2);

		cleanup1();
		expect(bus.getInFlightDrawCount("player1")).toBe(1);

		// Idempotent cleanup: calling again does not decrement below expected
		cleanup1();
		expect(bus.getInFlightDrawCount("player1")).toBe(1);

		cleanup2();
		expect(bus.getInFlightDrawCount("player1")).toBe(0);
	});

	it("increments and decrements in-flight play count per user", () => {
		const bus = new CardBus();
		expect(bus.getInFlightPlayCount("player1")).toBe(0);

		const cleanup1 = bus.addInFlightPlay("player1");
		expect(bus.getInFlightPlayCount("player1")).toBe(1);
		expect(bus.getInFlightPlayCount("player2")).toBe(0);

		const cleanup2 = bus.addInFlightPlay("player2");
		expect(bus.getInFlightPlayCount("player1")).toBe(1);
		expect(bus.getInFlightPlayCount("player2")).toBe(1);

		cleanup1();
		expect(bus.getInFlightPlayCount("player1")).toBe(0);
		expect(bus.getInFlightPlayCount("player2")).toBe(1);

		// Idempotent cleanup
		cleanup1();
		expect(bus.getInFlightPlayCount("player1")).toBe(0);

		cleanup2();
		expect(bus.getInFlightPlayCount("player2")).toBe(0);
	});

	it("triggers reactive derivations when in-flight counters change", () => {
		const bus = new CardBus();
		let observedDrawCount = -1;
		let observedPlayCount = -1;

		const dispose = $effect.root(() => {
			$effect(() => {
				observedDrawCount = bus.getInFlightDrawCount("player1");
				observedPlayCount = bus.getInFlightPlayCount("player1");
			});
		});

		flushSync();
		expect(observedDrawCount).toBe(0);
		expect(observedPlayCount).toBe(0);

		const cleanupDraw = bus.addInFlightDraw("player1");
		flushSync();
		expect(observedDrawCount).toBe(1);

		const cleanupPlay = bus.addInFlightPlay("player1");
		flushSync();
		expect(observedPlayCount).toBe(1);

		cleanupDraw();
		flushSync();
		expect(observedDrawCount).toBe(0);

		cleanupPlay();
		flushSync();
		expect(observedPlayCount).toBe(0);

		dispose();
	});
});
