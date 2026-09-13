import { describe, it, expect } from "vitest";
import { buildDrawBeats, drawStaggerFor, opponentSeatAnchor, localHandSlotAnchor } from "$components/game/animation/baseBeats.svelte";
import type { BoardPlacement } from "$components/game/layout/boardPlacement";

const placement: BoardPlacement = {
	mat: {} as never,
	handScale: 2,
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

describe("drawStaggerFor", () => {
	it("uses the tight stagger for a plain draw or a +2", () => {
		expect(drawStaggerFor(1)).toBe(0.1);
		expect(drawStaggerFor(2)).toBe(0.1);
	});

	it("slows down for a +4 or worse", () => {
		expect(drawStaggerFor(4)).toBe(0.5);
		expect(drawStaggerFor(5)).toBe(0.5);
	});
});

describe("buildDrawBeats", () => {
	it("builds a single beat with a flip-then-move per card for a local draw", () => {
		const beats = buildDrawBeats({ cardIds: ["card-9"], forLocalPlayer: true, placement });
		expect(beats).toHaveLength(1);
		expect(beats[0].map((s) => s.op)).toEqual(["flip", "move"]);

		const move = beats[0][1];
		expect(move.payload?.to).toBe("local-hand-slot:0");
		expect(move.payload?.toScale).toBe(placement.handScale);
		expect(move.payload?.toSpinDeg).toBe(0);
	});

	it("stacks multiple local cards in the same beat, staggered by drawStaggerFor", () => {
		const beats = buildDrawBeats({ cardIds: ["a", "b"], forLocalPlayer: true, placement });
		expect(beats).toHaveLength(1);
		const [flipA, moveA, flipB, moveB] = beats[0];
		expect(flipA.target).toBe("a");
		expect(moveA.target).toBe("a");
		expect(moveA.payload?.to).toBe("local-hand-slot:0");
		expect(flipB.target).toBe("b");
		expect(moveB.target).toBe("b");
		expect(moveB.payload?.to).toBe("local-hand-slot:1");
		expect(flipA.atS).toBe(0);
		expect(flipB.atS).toBe(drawStaggerFor(2));
		expect(moveB.atS).toBeGreaterThan(flipB.atS!);
	});

	it("builds a single covered move per card for an opponent draw (no flip)", () => {
		const beats = buildDrawBeats({
			cardIds: ["card-10", "card-11"],
			forLocalPlayer: false,
			opponentUsername: "bob",
			placement
		});
		expect(beats).toHaveLength(1);
		expect(beats[0].map((s) => s.op)).toEqual(["move", "move"]);
		expect(beats[0][0].payload?.to).toBe("seat:bob");
		expect(beats[0][1].atS).toBe(drawStaggerFor(2));
	});

	it("throws for an opponent draw missing a username", () => {
		expect(() => buildDrawBeats({ cardIds: ["x"], forLocalPlayer: false, placement })).toThrow();
	});
});

describe("localHandSlotAnchor", () => {
	it("includes the row's own Y-stack offset, matching LocalHand3D's own stacking", () => {
		const snapshot = { orderIds: [], scrollEm: 0, maxHalfSpanEm: 10 };
		const [, y] = localHandSlotAnchor(3, 2, placement, snapshot);
		expect(y).toBeCloseTo(2 * 0.02);
	});
});

describe("opponentSeatAnchor", () => {
	it("resolves a username to a seat anchor key consistently", () => {
		expect(opponentSeatAnchor("bob")).toBe("seat:bob");
	});
});
