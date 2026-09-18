import { describe, it, expect } from "vitest";
import { formatTurnTimer } from "$utils/timeFormat";

describe("formatTurnTimer", () => {
	it("formats sub-minute durations as 0:SS", () => {
		expect(formatTurnTimer(0)).toBe("0:00");
		expect(formatTurnTimer(5)).toBe("0:05");
		expect(formatTurnTimer(15)).toBe("0:15");
		expect(formatTurnTimer(59)).toBe("0:59");
	});

	it("rolls over into minutes past 60 seconds instead of staying 00:MM", () => {
		expect(formatTurnTimer(60)).toBe("1:00");
		expect(formatTurnTimer(75)).toBe("1:15");
		expect(formatTurnTimer(125)).toBe("2:05");
	});

	it("supports unbounded (multi-digit) minutes", () => {
		expect(formatTurnTimer(723)).toBe("12:03");
	});

	it("clamps negative input to 0:00", () => {
		expect(formatTurnTimer(-4)).toBe("0:00");
	});
});
