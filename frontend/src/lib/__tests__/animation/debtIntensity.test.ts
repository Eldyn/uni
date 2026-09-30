import { describe, it, expect } from "vitest";
import {
	debtIntensity,
	debtTremorOffset,
	debtDustMoteCount,
	debtMotePose
} from "$components/game/animation/debtIntensity";

describe("debtIntensity", () => {
	it.each([
		[0, 0],
		[-2, 0],
		[1, 1],
		[3, 1],
		[4, 2],
		[7, 2],
		[8, 3],
		[20, 3]
	])("maps %i pending draws to tier %i", (pendingDraws, tier) => {
		expect(debtIntensity(pendingDraws)).toBe(tier);
	});
});

describe("debt pile reaction helpers", () => {
	it("has no tremor and no motes at tier 0", () => {
		expect(Math.abs(debtTremorOffset(0, 0.3))).toBe(0);
		expect(debtDustMoteCount(0)).toBe(0);
	});

	it("escalates tremor amplitude and mote count with the tier", () => {
		const peakAt = (tier: 1 | 2 | 3) => {
			let peak = 0;
			for (let step = 0; step < 200; step++) {
				peak = Math.max(peak, Math.abs(debtTremorOffset(tier, step * 0.01)));
			}
			return peak;
		};
		expect(peakAt(2)).toBeGreaterThan(peakAt(1));
		expect(peakAt(3)).toBeGreaterThan(peakAt(2));
		expect(debtDustMoteCount(3)).toBeGreaterThan(debtDustMoteCount(2));
		expect(debtDustMoteCount(2)).toBeGreaterThan(debtDustMoteCount(1));
	});

	it("keeps mote opacity within 0..1 and rising height non-negative", () => {
		for (let index = 0; index < 12; index++) {
			for (let step = 0; step < 50; step++) {
				const pose = debtMotePose(index, step * 0.1);
				expect(pose.opacity).toBeGreaterThanOrEqual(0);
				expect(pose.opacity).toBeLessThanOrEqual(1);
				expect(pose.offsetY).toBeGreaterThanOrEqual(0);
			}
		}
	});
});
