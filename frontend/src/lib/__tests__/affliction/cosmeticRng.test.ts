import { describe, it, expect, vi } from "vitest";
import { CosmeticRng } from "$components/game/affliction/cosmeticRng";

describe("CosmeticRng: determinism", () => {
	it("same seed + same call order yields identical output", () => {
		const a = new CosmeticRng("seed-1");
		const b = new CosmeticRng("seed-1");
		const seqA = Array.from({ length: 16 }, () => a.nextInt(1000));
		const seqB = Array.from({ length: 16 }, () => b.nextInt(1000));
		expect(seqA).toEqual(seqB);
	});

	it("different seeds diverge", () => {
		const a = new CosmeticRng("seed-1");
		const b = new CosmeticRng("seed-2");
		const seqA = Array.from({ length: 16 }, () => a.nextInt(1000));
		const seqB = Array.from({ length: 16 }, () => b.nextInt(1000));
		expect(seqA).not.toEqual(seqB);
	});

	it("is independent of a sibling instance's draws", () => {
		const a = new CosmeticRng("seed");
		const expected = Array.from({ length: 8 }, () => a.nextInt(1_000_000));
		const noise = new CosmeticRng("noise");
		for (let i = 0; i < 5; i++) noise.nextInt(7);
		const b = new CosmeticRng("seed");
		const draws = Array.from({ length: 8 }, () => b.nextInt(1_000_000));
		expect(draws).toEqual(expected);
	});

	it("never touches Math.random", () => {
		const spy = vi.spyOn(Math, "random");
		const rng = new CosmeticRng("seed");
		for (let i = 0; i < 10; i++) rng.nextInt(100);
		expect(spy).not.toHaveBeenCalled();
		spy.mockRestore();
	});

	it("stays within [0, n) for a fixed seed", () => {
		const rng = new CosmeticRng("range");
		for (let i = 0; i < 500; i++) {
			const v = rng.nextInt(7);
			expect(Number.isInteger(v)).toBe(true);
			expect(v).toBeGreaterThanOrEqual(0);
			expect(v).toBeLessThan(7);
		}
	});

	it("matches an independent splitmix64 golden sequence", () => {
		const rng = new CosmeticRng("uni");
		expect(Array.from({ length: 4 }, () => rng.nextInt(1_000_000))).toEqual([
			472829, 380295, 881050, 396737
		]);
	});
});

describe("CosmeticRng: pick", () => {
	it("picks deterministically from a list", () => {
		const xs = ["a", "b", "c", "d"];
		const a = new CosmeticRng("pick");
		const b = new CosmeticRng("pick");
		expect(Array.from({ length: 10 }, () => a.pick(xs))).toEqual(
			Array.from({ length: 10 }, () => b.pick(xs))
		);
	});

	it("returns only elements of the list", () => {
		const xs = [1, 2, 3];
		const rng = new CosmeticRng("el");
		for (let i = 0; i < 50; i++) expect(xs).toContain(rng.pick(xs));
	});

	it("degrades on an empty list without throwing", () => {
		const rng = new CosmeticRng("empty");
		expect(() => rng.pick([])).not.toThrow();
	});
});
