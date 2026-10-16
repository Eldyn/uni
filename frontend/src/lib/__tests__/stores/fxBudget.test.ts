import { describe, it, expect, vi, afterEach } from "vitest";
import {
	clampEnvInt,
	readEnvInt,
	storeFxBudget,
	DEFAULT_FX_BUDGET,
	MIN_FX_BUDGET
} from "$stores/fxBudget.svelte";
import { EntityRuntime } from "$components/game/entity/entityRuntime.svelte";
import { AfflictionManager } from "$components/game/affliction/afflictionManager";
import { kindTarget } from "$components/game/affliction/compose";

describe("fxBudget env parsing (clamp + floor)", () => {
	afterEach(() => vi.unstubAllEnvs());

	it("falls back on blank / non-finite values", () => {
		expect(clampEnvInt(undefined, 64)).toBe(64);
		expect(clampEnvInt(null, 64)).toBe(64);
		expect(clampEnvInt("", 64)).toBe(64);
		expect(clampEnvInt("not-a-number", 64)).toBe(64);
		expect(clampEnvInt(Number.NaN, 64)).toBe(64);
		expect(clampEnvInt(Number.POSITIVE_INFINITY, 64)).toBe(64);
	});

	it("clamps non-positive overrides up to the positive floor", () => {
		expect(clampEnvInt("0", 64)).toBe(MIN_FX_BUDGET);
		expect(clampEnvInt(0, 64)).toBe(MIN_FX_BUDGET);
		expect(clampEnvInt("-5", 64)).toBe(MIN_FX_BUDGET);
	});

	it("floors a fractional override to an integer", () => {
		expect(clampEnvInt("3.9", 64)).toBe(3);
		expect(clampEnvInt(7.8, 8)).toBe(7);
	});

	it("keeps a valid positive integer as-is", () => {
		expect(clampEnvInt("12", 64)).toBe(12);
		expect(clampEnvInt(64, 8)).toBe(64);
	});

	it("readEnvInt reads a VITE_-prefixed override and clamps it", () => {
		vi.stubEnv("VITE_UNI_FX_MAX_ENTITIES", "0");
		expect(readEnvInt("UNI_FX_MAX_ENTITIES", 64)).toBe(MIN_FX_BUDGET);

		vi.stubEnv("VITE_UNI_FX_MAX_ENTITIES", "7.8");
		expect(readEnvInt("UNI_FX_MAX_ENTITIES", 64)).toBe(7);

		vi.stubEnv("VITE_UNI_FX_MAX_ENTITIES", "");
		expect(readEnvInt("UNI_FX_MAX_ENTITIES", 64)).toBe(64);
	});
});

describe("fxBudget consumers honour injected budget values", () => {
	afterEach(() => {
		storeFxBudget.maxEntities = DEFAULT_FX_BUDGET.maxEntities;
		storeFxBudget.maxAfflictions = DEFAULT_FX_BUDGET.maxAfflictions;
		storeFxBudget.screenStackMax = DEFAULT_FX_BUDGET.screenStackMax;
	});

	it("an EntityRuntime built from storeFxBudget.maxEntities enforces the lowered cap", () => {
		const warn = vi.spyOn(console, "warn").mockImplementation(() => {});
		storeFxBudget.maxEntities = 1;
		const runtime = new EntityRuntime({ maxEntities: storeFxBudget.maxEntities });
		runtime.define("dice", { components: { "lifetime.untilDespawn": {} } });

		expect(runtime.spawn("dice")).toBeGreaterThan(0);
		expect(runtime.spawn("dice")).toBe(0);
		expect(runtime.liveCount).toBe(1);
		warn.mockRestore();
	});

	it("an AfflictionManager built from storeFxBudget caps drops the lowest additive", () => {
		storeFxBudget.maxAfflictions = 1;
		storeFxBudget.screenStackMax = 1;
		const manager = new AfflictionManager({
			caps: {
				maxAfflictions: storeFxBudget.maxAfflictions,
				screenStackMax: storeFxBudget.screenStackMax
			}
		});

		manager.activate({
			presentationId: "lo",
			target: "card:c1",
			priority: 1,
			activationSeq: 0,
			modifiers: [{ kind: "particles.emitter", n: 1 }]
		});
		manager.activate({
			presentationId: "hi",
			target: "card:c1",
			priority: 2,
			activationSeq: 0,
			modifiers: [{ kind: "particles.emitter", n: 2 }]
		});

		const resolved = manager.resolve(kindTarget("particles.emitter", "card:c1")) as { n: number }[];
		expect(resolved).toHaveLength(1);
		expect(resolved[0].n).toBe(2);
	});
});
