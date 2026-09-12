import { describe, it, expect, afterEach } from "vitest";
import { flushSync } from "svelte";
import { createBaseBeatsWatcher } from "$components/game/animation/baseBeats.svelte";
import { CardBus } from "$components/game/card-bus.svelte";
import { AnimationQueue } from "$components/game/animation/animationQueue.svelte";
import { storeGame } from "$stores/game.svelte";
import type { BoardPlacement } from "$components/game/layout/boardPlacement";

// Regression test for the exact class of bug found during manual
// browser verification: createBaseBeatsWatcher's $effect/$effect.root calls
// only compile into real reactive code when the file the Svelte compiler
// actually processes ends in `.svelte.ts` — a plain `.ts` file leaves them as
// literal, undefined global function calls, throwing
// `ReferenceError: $effect is not defined` the instant the watcher is
// constructed. The other baseBeats.*.test.ts files only exercise the pure
// exported helpers, never createBaseBeatsWatcher itself, so none of them
// would have caught this.

const placement: BoardPlacement = {
	mat: {} as never,
	handScale: 1,
	centerScale: 1,
	discardX: 0,
	discardZ: 0,
	localSeatZ: 5,
	localAvatarZ: 6,
	drawPileX: -3,
	drawPileZ: 5,
	drawPileScale: 1,
	drawPileBesideHand: true
};

describe("createBaseBeatsWatcher", () => {
	afterEach(() => {
		storeGame.state = null;
	});

	it("constructs via $effect.root and its inner $effect fires without throwing", () => {
		storeGame.state = {
			active_type: "red",
			current_turn: "me",
			play_direction: 1,
			top_card: { id: 1, type: "red", value: "5" },
			players: [{ username: "me", card_count: 5, is_bot: false }],
			pending_draws: 0,
			draw_pile_size: 10
		} as never;

		const bus = new CardBus();
		const animationQueue = new AnimationQueue();

		let dispose: () => void = () => {};
		expect(() => {
			dispose = createBaseBeatsWatcher({
				bus,
				animationQueue,
				getPlacement: () => placement,
				getOpponentSeatAnchor: () => [0, 0, 0]
			});
			flushSync();
		}).not.toThrow();

		// The effect ran at least once: on its first pass (no previous top card
		// tracked yet), it hands the top card straight to the bus rather than
		// queuing a play animation for it — proof the watcher's body actually
		// executed, not just that construction didn't throw.
		expect(bus.discardTop).toEqual({ id: 1, type: "red", value: "5" });

		dispose();
	});
});
