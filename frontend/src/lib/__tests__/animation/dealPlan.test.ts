import { describe, it, expect, vi } from "vitest";
import type { Card, GamePlayer } from "$stores/game.svelte";
import {
	dealOrder,
	startingHandCount,
	dealStaggerFor,
	buildDealBeat,
	type BuildDealBeatArgs
} from "$components/game/animation/dealPlan";

function player(username: string, overrides: Partial<GamePlayer> = {}): GamePlayer {
	return { username, card_count: 0, is_bot: false, ...overrides };
}

function card(id: number): Card {
	return { id, type: "red", value: "0" };
}

function buildArgs(overrides: Partial<BuildDealBeatArgs> = {}): BuildDealBeatArgs {
	return {
		players: [player("alice"), player("bob"), player("carol")],
		povUsername: "alice",
		handCount: 2,
		localHand: [card(1), card(2)],
		localSlotPose: vi.fn(
			(slotIndex: number, handCount: number) =>
				[slotIndex, 0, handCount] as [number, number, number]
		),
		opponentPose: vi.fn((_username: string, count: number, slotIndex: number) => ({
			position: [count, slotIndex, 0] as [number, number, number],
			spinDeg: count * 10
		})),
		opponentCardScale: 0.5,
		handScale: 1.5,
		drawPileScale: 0.25,
		takeoffPose: vi.fn(() => [0, 0, 0] as [number, number, number]),
		onLocalLanded: vi.fn(),
		onOpponentLanded: vi.fn(),
		localCardId: (index) => `L${index}`,
		opponentCardId: (username, round) => `${username}-${round}`,
		...overrides
	};
}

describe("dealOrder", () => {
	const roster4 = [player("alice"), player("bob"), player("carol"), player("dave")];
	const roster3 = [player("alice"), player("bob"), player("carol")];

	it("puts the POV player last with opponents in turn order after them", () => {
		expect(dealOrder(roster4, "bob")).toEqual(["carol", "dave", "alice", "bob"]);
	});

	it("handles a POV player who is first in the raw array", () => {
		expect(dealOrder(roster4, "alice")).toEqual(["bob", "carol", "dave", "alice"]);
		expect(dealOrder(roster3, "alice")).toEqual(["bob", "carol", "alice"]);
	});

	it("puts the POV's next-in-turn-order opponent first", () => {
		expect(dealOrder(roster4, "carol")[0]).toBe("dave");
	});

	it("falls back to the raw order when the POV is absent, appending nothing twice", () => {
		expect(dealOrder(roster4, null)).toEqual(["alice", "bob", "carol", "dave"]);
		expect(dealOrder(roster4, undefined)).toEqual(["alice", "bob", "carol", "dave"]);
	});
});

describe("startingHandCount", () => {
	it("prefers the local hand length when non-empty", () => {
		expect(startingHandCount(7, [player("alice", { card_count: 3 })])).toBe(7);
	});

	it("falls back to the first player's card_count", () => {
		expect(startingHandCount(0, [player("alice", { card_count: 5 })])).toBe(5);
	});

	it("returns 0 for an empty hand and a zero count", () => {
		expect(startingHandCount(0, [player("alice", { card_count: 0 })])).toBe(0);
	});

	it("returns 0 with no players", () => {
		expect(startingHandCount(0, [])).toBe(0);
	});
});

describe("dealStaggerFor", () => {
	it("uses the base pace for a normal table", () => {
		expect(dealStaggerFor(3, 2)).toBe(0.07);
	});

	it("never drops below the floor for a very large deal", () => {
		const stagger = dealStaggerFor(20, 4);
		expect(stagger).toBeGreaterThanOrEqual(0.05);
		expect(stagger).toBeLessThanOrEqual(0.07);
	});
});

describe("buildDealBeat", () => {
	it("emits one flat beat, locals as flip+move and opponents as a single move", () => {
		const args = buildArgs();
		const { beat } = buildDealBeat(args);

		expect(beat).toHaveLength(8);
		expect(beat.map((s) => s.op)).toEqual([
			"move",
			"move",
			"flip",
			"move",
			"move",
			"move",
			"flip",
			"move"
		]);
		expect(beat.every((s) => typeof s.atS === "number")).toBe(true);
	});

	it("gives every card its own strictly increasing departure time", () => {
		const { beat } = buildDealBeat(buildArgs());
		const localIds = new Set(["L0", "L1"]);
		const departures = beat
			.filter((s) => s.op === "flip" || !localIds.has(s.target))
			.map((s) => s.atS as number);

		expect(departures).toHaveLength(6);
		for (let i = 1; i < departures.length; i++) {
			expect(departures[i]).toBeGreaterThan(departures[i - 1]);
		}
	});

	it("records every move target in anchors and references only those keys", () => {
		const args = buildArgs();
		const { beat, anchors } = buildDealBeat(args);

		expect([...anchors.keys()]).toEqual([
			"intro-opp:bob-0",
			"intro-opp:carol-0",
			"intro-local:L0",
			"intro-opp:bob-1",
			"intro-opp:carol-1",
			"intro-local:L1"
		]);
		for (const step of beat) {
			if (step.op !== "move") continue;
			const key = step.payload?.to as string;
			expect(anchors.has(key)).toBe(true);
		}
	});

	it("targets opponents with their growing round count and round slot", () => {
		const args = buildArgs();
		buildDealBeat(args);

		expect(args.opponentPose).toHaveBeenCalledWith("bob", 1, 0);
		expect(args.opponentPose).toHaveBeenCalledWith("carol", 1, 0);
		expect(args.opponentPose).toHaveBeenCalledWith("bob", 2, 1);
		expect(args.opponentPose).toHaveBeenCalledWith("carol", 2, 1);
		expect(args.localSlotPose).toHaveBeenCalledWith(0, 2);
		expect(args.localSlotPose).toHaveBeenCalledWith(1, 2);
	});

	it("carries the right scale, spin and face-up flip for each seat", () => {
		const { beat, anchors } = buildDealBeat(buildArgs());
		const [bob0, , alice0Flip, alice0Move] = beat;

		expect(bob0.payload?.toScale).toBe(0.5);
		expect(bob0.payload?.toSpinDeg).toBe(10);
		expect(anchors.get(bob0.payload?.to as string)).toEqual([1, 0, 0]);

		expect(alice0Flip.payload).toEqual({ turned: false, axis: "x" });
		expect(alice0Move.payload?.toScale).toBe(1.5);
		expect(alice0Move.payload?.toSpinDeg).toBe(0);
		expect(alice0Move.atS).toBeGreaterThan(alice0Flip.atS as number);
	});

	it("invokes the matching landed callback from each move", () => {
		const args = buildArgs();
		const { beat } = buildDealBeat(args);

		(beat[0].payload?.onComplete as () => void)();
		expect(args.onOpponentLanded).toHaveBeenCalledWith("bob", 0);

		(beat[3].payload?.onComplete as () => void)();
		expect(args.onLocalLanded).toHaveBeenCalledWith(0);

		(beat[7].payload?.onComplete as () => void)();
		expect(args.onLocalLanded).toHaveBeenCalledWith(1);
	});
});
