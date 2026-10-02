import { describe, it, expect } from "vitest";
import { DESKTOP_MAT_SHEET, PHONE_MAT_SHEET } from "$components/game/layout/playmat";

describe("mat sheets", () => {
	it("measures both current sheets as 1920x1080", () => {
		for (const sheet of [DESKTOP_MAT_SHEET, PHONE_MAT_SHEET]) {
			expect(sheet.texelWidth).toBe(1920);
			expect(sheet.texelHeight).toBe(1080);
		}
	});

	// magick playmat.png -alpha extract -threshold 10% -format %@ info: -> 640x464+640+300
	it("places the desktop felt at 640..1280 x 300..764 texels, v measured upward", () => {
		const rect = DESKTOP_MAT_SHEET.feltUvRect;
		expect(rect.left).toBeCloseTo(640 / 1920);
		expect(rect.right).toBeCloseTo(1280 / 1920);
		expect(rect.top).toBeCloseTo(1 - 300 / 1080);
		expect(rect.bottom).toBeCloseTo(1 - 764 / 1080);
	});

	// magick mobile_playmat.png -alpha extract -threshold 10% -format %@ info: -> 687x464+621+298
	it("places the phone felt at 621..1308 x 298..762 texels", () => {
		const rect = PHONE_MAT_SHEET.feltUvRect;
		expect(rect.left).toBeCloseTo(621 / 1920);
		expect(rect.right).toBeCloseTo(1308 / 1920);
		expect(rect.top).toBeCloseTo(1 - 298 / 1080);
		expect(rect.bottom).toBeCloseTo(1 - 762 / 1080);
	});

	it("keeps top above bottom in v for both sheets", () => {
		for (const sheet of [DESKTOP_MAT_SHEET, PHONE_MAT_SHEET]) {
			expect(sheet.feltUvRect.top).toBeGreaterThan(sheet.feltUvRect.bottom);
		}
	});
});
