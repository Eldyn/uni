import { describe, it, expect } from "vitest";
import { DESKTOP_MAT_SHEET, PHONE_MAT_SHEET } from "$components/game/layout/playmat";

describe("mat sheets", () => {
	it("sizes the desktop sheet 1920x1080 and the phone sheet 1080x1920", () => {
		expect(DESKTOP_MAT_SHEET.texelWidth).toBe(1920);
		expect(DESKTOP_MAT_SHEET.texelHeight).toBe(1080);
		expect(PHONE_MAT_SHEET.texelWidth).toBe(1080);
		expect(PHONE_MAT_SHEET.texelHeight).toBe(1920);
	});

	// magick playmat.png -alpha extract -threshold 10% -format %@ info: -> 648x480+648+300
	it("places the desktop felt at 648..1296 x 300..780 texels, v measured upward", () => {
		const rect = DESKTOP_MAT_SHEET.feltUvRect;
		expect(rect.left).toBeCloseTo(648 / 1920);
		expect(rect.right).toBeCloseTo(1296 / 1920);
		expect(rect.top).toBeCloseTo(1 - 300 / 1080);
		expect(rect.bottom).toBeCloseTo(1 - 780 / 1080);
	});

	// magick mobile_playmat.png -alpha extract -threshold 10% -format %@ info: -> 480x648+300+624
	it("places the phone felt at 300..780 x 624..1272 texels", () => {
		const rect = PHONE_MAT_SHEET.feltUvRect;
		expect(rect.left).toBeCloseTo(300 / 1080);
		expect(rect.right).toBeCloseTo(780 / 1080);
		expect(rect.top).toBeCloseTo(1 - 624 / 1920);
		expect(rect.bottom).toBeCloseTo(1 - 1272 / 1920);
	});

	it("keeps top above bottom in v for both sheets", () => {
		for (const sheet of [DESKTOP_MAT_SHEET, PHONE_MAT_SHEET]) {
			expect(sheet.feltUvRect.top).toBeGreaterThan(sheet.feltUvRect.bottom);
		}
	});
});
