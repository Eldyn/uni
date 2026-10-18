import { describe, expect, it } from "vitest";
import { encodePng } from "../../../../scripts/avatar-placeholders.mjs";
import { buildAsepriteArgs, checkStrip } from "../../../../scripts/avatar-export.mjs";

/** A fully transparent RGBA buffer for the given pixel size. */
function blankRgba(width: number, height: number): Uint8Array {
	return new Uint8Array(width * height * 4);
}

describe("buildAsepriteArgs", () => {
	it("returns the batch sheet arguments", () => {
		expect(buildAsepriteArgs("art/dot.aseprite", "out/dot.png")).toEqual([
			"-b",
			"art/dot.aseprite",
			"--sheet",
			"out/dot.png",
			"--sheet-type",
			"horizontal"
		]);
	});
});

describe("checkStrip", () => {
	it("accepts a 480x96 png header", () => {
		expect(checkStrip(encodePng(480, 96, blankRgba(480, 96)))).toEqual([]);
	});

	it("reports the actual size for a wrong one", () => {
		const problems = checkStrip(encodePng(96, 96, blankRgba(96, 96)));

		expect(problems).toHaveLength(1);
		expect(problems[0]).toContain("96x96");
	});

	it("reports a non-png buffer", () => {
		const problems = checkStrip(Buffer.from("not a png"));

		expect(problems).toHaveLength(1);
		expect(problems[0].toLowerCase()).toContain("png");
	});
});
