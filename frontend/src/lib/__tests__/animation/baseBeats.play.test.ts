import { describe, it, expect, vi } from "vitest";
import { buildPlayBeat, localCardAnchor } from "$components/game/animation/baseBeats.svelte";
import type { BoardPlacement } from "$components/game/layout/boardPlacement";

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

describe("buildPlayBeat", () => {
	it("builds a single-step move beat straight to the discard pile for the local player's own play", () => {
		const beat = buildPlayBeat({
			cardId: "card-1",
			playedByMe: true,
			placement,
			localHandSnapshot: { orderIds: [1], scrollEm: 0, maxHalfSpanEm: 10 },
			landingSpinDeg: 12
		});

		expect(beat).toHaveLength(1);
		expect(beat[0].op).toBe("move");
		expect(beat[0].target).toBe("card-1");
		expect(beat[0].payload?.to).toBe("discard-pile");
		expect(beat[0].payload?.toSpinDeg).toBe(12);
	});

	it("builds a flip-then-move beat for an opponent's play (covered until it lands)", () => {
		const beat = buildPlayBeat({
			cardId: "card-2",
			playedByMe: false,
			placement,
			localHandSnapshot: { orderIds: [], scrollEm: 0, maxHalfSpanEm: 10 },
			landingSpinDeg: 12
		});

		expect(beat.map((s) => s.op)).toEqual(["move", "flip"]);
		const flipStep = beat.find((s) => s.op === "flip")!;
		expect(flipStep.payload?.axis).toBe("y");
	});

	it("now also tweens toSpinDeg on an opponent's move step, since flip no longer touches spinDeg", () => {
		const beat = buildPlayBeat({
			cardId: "card-3",
			playedByMe: false,
			placement,
			localHandSnapshot: { orderIds: [], scrollEm: 0, maxHalfSpanEm: 10 },
			landingSpinDeg: 37
		});

		const moveStep = beat.find((s) => s.op === "move")!;
		expect(moveStep.payload?.toSpinDeg).toBe(37);
	});
});

describe("localCardAnchor", () => {
	it("falls back to the previous snapshot when the card is missing from the current one", () => {
		const current = { orderIds: [2, 3], scrollEm: 0, maxHalfSpanEm: 10 };
		const previous = { orderIds: [1, 2, 3], scrollEm: 0, maxHalfSpanEm: 10 };

		const fromPrevious = localCardAnchor(1, placement, previous, null);
		const fromCurrentMiss = localCardAnchor(1, placement, current, previous);

		expect(fromCurrentMiss).toEqual(fromPrevious);
	});

	it("warns and falls back to hand-center when the card is in neither snapshot", () => {
		const warn = vi.spyOn(console, "warn").mockImplementation(() => {});
		const [x] = localCardAnchor(999, placement, { orderIds: [], scrollEm: 0, maxHalfSpanEm: 10 }, null);
		expect(x).toBe(0);
		expect(warn).toHaveBeenCalled();
		warn.mockRestore();
	});

	it("includes the found slot's own Y-stack offset", () => {
		const snapshot = { orderIds: [5, 6, 7], scrollEm: 0, maxHalfSpanEm: 10 };
		const [, y] = localCardAnchor(7, placement, snapshot, null);
		expect(y).toBeCloseTo(2 * 0.02);
	});
});
