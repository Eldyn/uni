import { describe, it, expect } from "vitest";
import {
	resolveOutcome,
	outcomeKeyOf,
	type OutcomeDescriptor
} from "$components/game/entity/outcomeResolver";

/** The resolvers mirror `mods/test/entities/dice.json`. */
const resolver = {
	"test:six": { animation: "land_6", sound: "clack" },
	"*": { animation: "land_1" }
};

describe("resolveOutcome: selection order", () => {
	it("picks the exact descriptor for an exact key", () => {
		expect(resolveOutcome(resolver, "test:six")).toEqual({
			animation: "land_6",
			sound: "clack"
		});
	});

	it("falls back to the `*` catch-all when no exact key matches", () => {
		expect(resolveOutcome(resolver, "test:rare")).toEqual({ animation: "land_1" });
	});

	it("returns undefined when neither the exact key nor `*` is present", () => {
		const noStar = { "test:six": { animation: "land_6" } };
		expect(resolveOutcome(noStar, "test:rare")).toBeUndefined();
	});

	it("keeps an empty `*` descriptor distinct from a miss", () => {
		expect(resolveOutcome({ "test:six": {}, "*": {} }, "test:six")).toEqual({});
		expect(resolveOutcome({ "test:six": {}, "*": {} }, "test:other")).toEqual({});
	});

	it("degrades a malformed resolver to undefined, never throws", () => {
		expect(
			resolveOutcome(undefined as unknown as Record<string, OutcomeDescriptor>, "x")
		).toBeUndefined();
		expect(
			resolveOutcome(null as unknown as Record<string, OutcomeDescriptor>, "x")
		).toBeUndefined();
		expect(resolveOutcome({ "test:six": 7, "*": {} }, "test:six")).toEqual({});
	});

	it("ignores a non-object exact value and falls through to `*`", () => {
		expect(resolveOutcome({ "test:six": null, "*": { ok: true } }, "test:six")).toEqual({
			ok: true
		});
	});
});

describe("outcomeKeyOf: the bound `$last_outcome` reference", () => {
	it("reads a string `outcome` field", () => {
		expect(outcomeKeyOf({ outcome: "test:six" })).toBe("test:six");
	});

	it("reads `last_outcome.key` from the roll_table binding shape", () => {
		expect(outcomeKeyOf({ last_outcome: { table: "test:loot", key: "test:six", value: 6 } })).toBe(
			"test:six"
		);
	});

	it("returns undefined for a missing or malformed binding", () => {
		expect(outcomeKeyOf({})).toBeUndefined();
		expect(outcomeKeyOf({ outcome: 7 })).toBeUndefined();
		expect(outcomeKeyOf({ last_outcome: {} })).toBeUndefined();
		expect(outcomeKeyOf(null)).toBeUndefined();
	});
});
