import { describe, it, expect } from "vitest";
import { isHandCardDimmed } from "$components/game/layout/handCardDimming";

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
});
