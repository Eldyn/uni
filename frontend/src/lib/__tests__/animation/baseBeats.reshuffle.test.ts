import { describe, it, expect } from "vitest";
import { detectReshuffle, buildReshuffleBeat } from "$components/game/animation/baseBeats";
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

describe("buildReshuffleBeat", () => {
	it("moves every discard entry except the top one back onto the draw pile, in one beat", () => {
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
	});
});
