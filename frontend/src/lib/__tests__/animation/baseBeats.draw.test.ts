import { describe, it, expect } from "vitest";
import { buildDrawBeat, opponentSeatAnchor } from "$components/game/animation/baseBeats.svelte";

describe("buildDrawBeat", () => {
	it("builds a flip-onto-itself beat for a local draw", () => {
		const beat = buildDrawBeat({ cardId: "card-9", forLocalPlayer: true });
		expect(beat.map((s) => s.op)).toEqual(["move", "flip"]);
		expect(beat[0].payload?.to).toBe("local-hand-rightmost");
	});

	it("builds a covered move for an opponent draw (no flip)", () => {
		const beat = buildDrawBeat({ cardId: "card-10", forLocalPlayer: false, opponentUsername: "bob" });
		expect(beat.map((s) => s.op)).toEqual(["move"]);
		expect(beat[0].payload?.to).toBe("seat:bob");
	});
});

describe("opponentSeatAnchor", () => {
	it("resolves a username to a seat anchor key consistently", () => {
		expect(opponentSeatAnchor("bob")).toBe("seat:bob");
	});
});
