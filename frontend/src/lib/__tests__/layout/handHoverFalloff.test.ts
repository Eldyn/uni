import { describe, it, expect } from "vitest";
import { neighborPushEm } from "$components/game/layout/handHoverFalloff";

describe("neighborPushEm", () => {
	it("returns 0 for the active index itself", () => {
		expect(neighborPushEm(3, 3, { amplitudeEm: 2, decay: 2.5 })).toBe(0);
	});

	it("pushes left neighbors negatively and right neighbors positively", () => {
		const left = neighborPushEm(2, 3, { amplitudeEm: 2, decay: 2.5 });
		const right = neighborPushEm(4, 3, { amplitudeEm: 2, decay: 2.5 });
		expect(left).toBeLessThan(0);
		expect(right).toBeGreaterThan(0);
		expect(Math.abs(left)).toBeCloseTo(right);
	});

	it("decays continuously with distance without hard cutoff", () => {
		const n1 = Math.abs(neighborPushEm(4, 3, { amplitudeEm: 2, decay: 2.5 }));
		const n2 = Math.abs(neighborPushEm(5, 3, { amplitudeEm: 2, decay: 2.5 }));
		const n5 = Math.abs(neighborPushEm(8, 3, { amplitudeEm: 2, decay: 2.5 }));
		expect(n1).toBeGreaterThan(n2);
		expect(n2).toBeGreaterThan(n5);
		expect(n5).toBeGreaterThan(0);
	});

	it("immediate neighbor gets full amplitude", () => {
		const push = neighborPushEm(4, 3, { amplitudeEm: 2, decay: 2.5 });
		expect(push).toBeCloseTo(2);
	});
});
