import { describe, it, expect } from "vitest";
import {
	BACKDROP_FRAGMENT_SOURCE,
	BACKDROP_GRID_COLUMNS,
	BACKDROP_MAX_SHADE,
	BACKDROP_SHIFT_STEP_S
} from "$lib/shop/backdrop.glsl";
import homeFragment from "$lib/shaders/rippleQuad.frag.glsl?raw";

const source = BACKDROP_FRAGMENT_SOURCE;

describe("backdrop fragment source", () => {
	it("starts with the GLSL ES 3.00 version line", () => {
		expect(source.startsWith("#version 300 es\n")).toBe(true);
	});

	it("declares only the dither uniforms", () => {
		for (const name of ["uTime", "uResolution", "uBase", "uHint", "uShade"]) {
			expect(source).toMatch(new RegExp(`uniform [a-z0-9]+ ${name};`));
		}
		for (const removed of ["uDim", "uPixelSize", "uAccent"]) {
			expect(source).not.toContain(removed);
		}
	});

	it("has no bands, gradient or dust", () => {
		expect(source).not.toMatch(/\bsin\(/);
		for (const removed of ["smoothstep", "hash(", "BAND_", "GRADIENT", "DUST", "lift"]) {
			expect(source).not.toContain(removed);
		}
	});

	it("uses the home Bayer matrix and pixel grid", () => {
		expect(source).toContain("float bayer4(vec2 pixelCoord)");
		expect(source).toContain("bayer4(cell + shift) + 0.0001");
		expect(BACKDROP_GRID_COLUMNS).toBe(180);
		expect(homeFragment).toContain("const float GRID_COLUMNS = 180.0;");
		expect(source).toContain("const float GRID_COLUMNS = 180.0;");
		expect(source).toContain("floor(gl_FragCoord.xy / cellSize)");
	});

	it("paints exactly one of the two palette colors per cell", () => {
		expect(source).toContain("float dark = step(threshold, shade);");
		expect(source).toContain("mix(uHint, uBase, dark)");
		expect(source).not.toMatch(/\*=/);
		expect(BACKDROP_MAX_SHADE).toBeLessThanOrEqual(0.5);
		expect(source).toContain(`const float MAX_SHADE = ${BACKDROP_MAX_SHADE.toFixed(2)};`);
	});

	it("always writes alpha 1.0", () => {
		const writes = source.match(/fragColor\s*=[^;]*;/g) ?? [];
		expect(writes.length).toBeGreaterThan(0);
		for (const write of writes) {
			expect(write).toMatch(/^fragColor = vec4\(.*, 1\.0\);$/);
		}
		expect(source).not.toContain("discard");
	});

	it("moves only by a slow step shift of the pattern", () => {
		expect(BACKDROP_SHIFT_STEP_S).toBeGreaterThanOrEqual(1);
		expect(source).toContain("floor(uTime / SHIFT_STEP_S)");
	});

	it("has balanced braces", () => {
		expect(source.split("{").length).toBe(source.split("}").length);
	});
});
