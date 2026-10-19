import { describe, it, expect } from "vitest";
import {
	BACKDROP_BAND_A_PERIOD_S,
	BACKDROP_BAND_B_PERIOD_S,
	BACKDROP_FRAGMENT_SOURCE,
	BACKDROP_MAX_LIFT,
	BACKDROP_PIXEL_SIZE_PX
} from "$lib/shop/backdrop.glsl";

describe("backdrop fragment source", () => {
	it("declares the new uniforms and drops the accent", () => {
		for (const name of ["uTime", "uResolution", "uBase", "uHint", "uPixelSize", "uDim"]) {
			expect(BACKDROP_FRAGMENT_SOURCE).toContain(`uniform`);
			expect(BACKDROP_FRAGMENT_SOURCE).toMatch(new RegExp(`uniform [a-z0-9]+ ${name};`));
		}
		expect(BACKDROP_FRAGMENT_SOURCE).not.toContain("uAccent");
	});

	it("never feeds time into a hash", () => {
		const hashCalls = BACKDROP_FRAGMENT_SOURCE.match(/hash\([^;]*\)/g) ?? [];
		expect(hashCalls.length).toBeGreaterThan(0);
		for (const call of hashCalls) {
			expect(call).not.toContain("uTime");
		}
		expect(BACKDROP_FRAGMENT_SOURCE).not.toMatch(/vec2\(\s*uTime/);
	});

	it("keeps both band periods above 20 seconds", () => {
		expect(BACKDROP_BAND_A_PERIOD_S).toBeGreaterThan(20);
		expect(BACKDROP_BAND_B_PERIOD_S).toBeGreaterThan(20);
		expect(BACKDROP_FRAGMENT_SOURCE).toContain(
			`const float BAND_A_PERIOD_S = ${BACKDROP_BAND_A_PERIOD_S.toFixed(1)};`
		);
		expect(BACKDROP_FRAGMENT_SOURCE).toContain(
			`const float BAND_B_PERIOD_S = ${BACKDROP_BAND_B_PERIOD_S.toFixed(1)};`
		);
	});

	it("uses a 4 px grid and never lifts past the hint color", () => {
		expect(BACKDROP_PIXEL_SIZE_PX).toBe(4);
		expect(BACKDROP_MAX_LIFT).toBeLessThanOrEqual(1);
		expect(BACKDROP_FRAGMENT_SOURCE).toContain(
			`const float MAX_LIFT = ${BACKDROP_MAX_LIFT.toFixed(2)};`
		);
		expect(BACKDROP_FRAGMENT_SOURCE).toContain("mix(uBase, uHint, lift)");
	});

	it("divides the screen by the pixel size uniform, not a literal", () => {
		expect(BACKDROP_FRAGMENT_SOURCE).toContain("gl_FragCoord.xy / uPixelSize");
		expect(BACKDROP_FRAGMENT_SOURCE).not.toContain("PIXEL_GRID");
	});

	it("has balanced braces", () => {
		const open = BACKDROP_FRAGMENT_SOURCE.split("{").length;
		const close = BACKDROP_FRAGMENT_SOURCE.split("}").length;
		expect(open).toBe(close);
	});
});
