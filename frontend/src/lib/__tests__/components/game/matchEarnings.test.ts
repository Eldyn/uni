import { describe, it, expect } from "vitest";
import { localEarning, parsePayouts } from "$components/game/matchEarnings";

describe("parsePayouts", () => {
	it("keeps a username to amount map", () => {
		expect(parsePayouts({ alice: 100, bob: 20 })).toEqual({ alice: 100, bob: 20 });
	});

	it("returns an empty map for missing or malformed input", () => {
		expect(parsePayouts(undefined)).toEqual({});
		expect(parsePayouts({ alice: "100" })).toEqual({});
		expect(parsePayouts([1, 2])).toEqual({});
	});
});

describe("localEarning", () => {
	it("returns the local amount", () => {
		expect(localEarning({ alice: 100 }, "alice", false)).toBe(100);
	});

	it("returns null for a guest, a zero or a missing amount", () => {
		expect(localEarning({ alice: 100 }, "alice", true)).toBeNull();
		expect(localEarning({ alice: 0 }, "alice", false)).toBeNull();
		expect(localEarning({ bob: 20 }, "alice", false)).toBeNull();
		expect(localEarning(undefined, "alice", false)).toBeNull();
	});
});
