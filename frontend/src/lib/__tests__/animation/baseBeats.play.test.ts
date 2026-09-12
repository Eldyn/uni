import { describe, it, expect } from "vitest";
import { buildPlayBeat } from "$components/game/animation/baseBeats";
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
			localHandSnapshot: { orderIds: [1], scrollEm: 0, maxHalfSpanEm: 10 }
		});

		expect(beat).toHaveLength(1);
		expect(beat[0].op).toBe("move");
		expect(beat[0].target).toBe("card-1");
		expect(beat[0].payload?.to).toBe("discard-pile");
	});

	it("builds a flip-then-move beat for an opponent's play (covered until it lands)", () => {
		const beat = buildPlayBeat({
			cardId: "card-2",
			playedByMe: false,
			placement,
			localHandSnapshot: { orderIds: [], scrollEm: 0, maxHalfSpanEm: 10 }
		});

		expect(beat.map((s) => s.op)).toEqual(["move", "flip"]);
	});
});
