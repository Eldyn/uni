import { describe, it, expect } from "vitest";
import { shouldFireTurnCue } from "$components/game/animation/turnCue";

const base = { previousTurn: "bob", currentTurn: "me", localPlayerId: "me", isSpectator: false };

describe("shouldFireTurnCue", () => {
	it("fires when the turn changes to the local player", () => {
		expect(shouldFireTurnCue(base)).toBe(true);
	});

	it("fires when there is no previous turn and the local player is up", () => {
		expect(shouldFireTurnCue({ ...base, previousTurn: null })).toBe(true);
	});

	it("does not fire when the turn did not change", () => {
		expect(shouldFireTurnCue({ ...base, previousTurn: "me" })).toBe(false);
	});

	it("does not fire for another player's turn", () => {
		expect(shouldFireTurnCue({ ...base, currentTurn: "bob", previousTurn: "me" })).toBe(false);
	});

	it("does not fire for a spectator", () => {
		expect(shouldFireTurnCue({ ...base, isSpectator: true })).toBe(false);
	});
});
