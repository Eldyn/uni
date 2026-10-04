import { describe, it, expect } from "vitest";
import { isHandCardDimmed, isIdenticalToTop } from "$components/game/layout/handCardDimming";

describe("isHandCardDimmed", () => {
	it("keeps the whole hand lit while the hand is not dimmed", () => {
		expect(isHandCardDimmed({ handDimmed: false, isWindowResponder: false, canPlay: false })).toBe(
			false
		);
	});

	it("dims every card of an out-of-turn hand with no open window to answer", () => {
		expect(isHandCardDimmed({ handDimmed: true, isWindowResponder: false, canPlay: true })).toBe(
			true
		);
	});

	it("lights only the playable cards of an out-of-turn window responder", () => {
		expect(isHandCardDimmed({ handDimmed: true, isWindowResponder: true, canPlay: true })).toBe(
			false
		);
		expect(isHandCardDimmed({ handDimmed: true, isWindowResponder: true, canPlay: false })).toBe(
			true
		);
		expect(
			isHandCardDimmed({ handDimmed: true, isWindowResponder: true, canPlay: undefined })
		).toBe(true);
	});

	it("lights a jump-in candidate even with no window open", () => {
		expect(
			isHandCardDimmed({
				handDimmed: true,
				isWindowResponder: false,
				canPlay: false,
				isJumpInCandidate: true
			})
		).toBe(false);
	});

	it("dims the rest of the hand while the drawn card is held, even on-turn", () => {
		expect(
			isHandCardDimmed({
				handDimmed: false,
				isWindowResponder: false,
				canPlay: true,
				pendingPlayDrawn: true
			})
		).toBe(true);
		expect(
			isHandCardDimmed({
				handDimmed: false,
				isWindowResponder: true,
				canPlay: true,
				pendingPlayDrawn: true
			})
		).toBe(true);
	});
});

describe("isIdenticalToTop", () => {
	const red6 = { id: 1, type: "red", value: "6" } as const;

	it("matches the same colour and value", () => {
		expect(isIdenticalToTop({ ...red6, id: 2 }, red6)).toBe(true);
	});

	it("rejects a different colour, a different value and a missing top", () => {
		expect(isIdenticalToTop({ ...red6, type: "blue" }, red6)).toBe(false);
		expect(isIdenticalToTop({ ...red6, value: "5" }, red6)).toBe(false);
		expect(isIdenticalToTop(red6, undefined)).toBe(false);
	});

	it("never matches a wild top", () => {
		const wild = { id: 3, type: "white", value: "wild" } as const;
		expect(isIdenticalToTop({ ...wild, id: 4 }, wild)).toBe(false);
	});
});
