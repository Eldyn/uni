import { describe, expect, it } from "vitest";
import {
	DRAW_DEBT_STATUS_KIND,
	WILD_KIND_IDS,
	baseKindId,
	isBaseModNamespace
} from "$lib/utils/baseMod";

describe("base mod ids", () => {
	it("accepts the uni namespace", () => {
		expect(isBaseModNamespace("uni")).toBe(true);
	});

	it("rejects every other namespace", () => {
		expect(isBaseModNamespace("")).toBe(false);
		expect(isBaseModNamespace("seven_zero")).toBe(false);
		expect(isBaseModNamespace("unity")).toBe(false);
	});
});

describe("base mod kind constants", () => {
	it("builds base kind ids in the uni namespace", () => {
		expect(baseKindId("wild")).toBe("uni:wild");
		expect(WILD_KIND_IDS).toEqual(["uni:wild", "uni:wild_draw4"]);
	});

	it("names the uni draw debt status kind", () => {
		expect(DRAW_DEBT_STATUS_KIND).toBe("uni:draw_debt");
	});
});
