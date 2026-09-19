import { describe, it, expect } from "vitest";
import { isInspectable } from "$components/game/animation/inspectable";
import type { FlightHandle } from "$components/game/animation/cardRegistry.svelte";

function handle(overrides: Partial<FlightHandle> = {}): FlightHandle {
	return {
		id: "42",
		pose: { turned: false } as FlightHandle["pose"],
		card: { type: "red", value: "5" },
		...overrides
	} as FlightHandle;
}

describe("isInspectable", () => {
	it("allows a face-up, idle card", () => {
		expect(isInspectable(handle())).toBe(true);
	});

	it("rejects face-down cards (opponent rings, hidden backs)", () => {
		expect(isInspectable(handle({ pose: { turned: true } as FlightHandle["pose"] }))).toBe(false);
	});

	it("rejects opponent ring entries even if somehow face-up", () => {
		expect(isInspectable(handle({ id: "ring:alice:0" }))).toBe(false);
	});

	it("rejects cards mid-flight", () => {
		expect(isInspectable(handle(), true)).toBe(false);
	});
});
