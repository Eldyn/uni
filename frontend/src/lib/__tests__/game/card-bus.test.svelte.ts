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
