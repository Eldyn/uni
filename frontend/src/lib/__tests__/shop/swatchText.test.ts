import { describe, expect, it } from "vitest";
import { swatchName } from "$lib/shop/swatchText";

describe("swatchName", () => {
	it("uses the last id segment in title case", () => {
		expect(swatchName("uni:neon:hot")).toBe("Hot");
		expect(swatchName("basic:red")).toBe("Red");
	});

	it("keeps a single segment", () => {
		expect(swatchName("plain")).toBe("Plain");
	});
});
