import { describe, expect, it } from "vitest";
import {
	DRAW_DEBT_STATUS_KINDS,
	WILD_KIND_IDS,
	baseKindId,
	isBaseModNamespace,
	toLegacyBaseKindId
} from "$lib/utils/baseMod";

describe("base mod ids", () => {
	it("accepts the uni and the vanilla namespace", () => {
		expect(isBaseModNamespace("uni")).toBe(true);
		expect(isBaseModNamespace("vanilla")).toBe(true);
	});

	it("rejects every other namespace", () => {
		expect(isBaseModNamespace("")).toBe(false);
		expect(isBaseModNamespace("seven_zero")).toBe(false);
		expect(isBaseModNamespace("unity")).toBe(false);
	});

	it("maps a uni kind id to its vanilla form", () => {
		expect(toLegacyBaseKindId("uni:red_5")).toBe("vanilla:red_5");
		expect(toLegacyBaseKindId("vanilla:red_5")).toBe("vanilla:red_5");
	});

	it("leaves other kind ids unchanged", () => {
		expect(toLegacyBaseKindId("seven_zero:swap")).toBe("seven_zero:swap");
		expect(toLegacyBaseKindId("unity:red_5")).toBe("unity:red_5");
		expect(toLegacyBaseKindId("uni")).toBe("uni");
	});
});

describe("base mod kind constants", () => {
	it("builds base kind ids in the uni namespace", () => {
		expect(baseKindId("wild")).toBe("uni:wild");
		expect(WILD_KIND_IDS).toEqual(["uni:wild", "uni:wild_draw4"]);
	});

	it("lists the uni draw debt status kind", () => {
		expect(DRAW_DEBT_STATUS_KINDS).toContain("uni:draw_debt");
	});
});
