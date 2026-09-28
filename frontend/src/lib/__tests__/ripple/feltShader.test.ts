import { describe, it, expect } from "vitest";
import {
	buildFeltFragmentShader,
	WILD_FLASH_RADIUS_PLACEHOLDER
} from "$components/game/three/ripple/feltShader";

// The felt ShaderMaterial compiles as GLSL ES 1.00 (three.js adds no #version
// directive), which has no implicit int-to-float conversion: `const float X =
// 2;` is a compile error and makes the whole felt mesh render nothing. The
// emitted declaration must therefore be a float expression, never a bare int.
const FELT_FLASH_RADIUS_DECLARATION = /const\s+float\s+FLASH_RADIUS_BLOCKS\s*=\s*([^;]+);/;

describe("buildFeltFragmentShader", () => {
	it("fully resolves the flash-radius placeholder", () => {
		expect(buildFeltFragmentShader()).not.toContain(WILD_FLASH_RADIUS_PLACEHOLDER);
	});

	it("emits a float-valid FLASH_RADIUS_BLOCKS initializer, not a bare int literal", () => {
		const shader = buildFeltFragmentShader();
		const match = FELT_FLASH_RADIUS_DECLARATION.exec(shader);
		expect(match).not.toBeNull();

		const initializer = match![1].trim();
		expect(initializer).not.toMatch(/^-?\d+$/);
		expect(initializer).toMatch(/^(?:float\s*\(\s*-?\d+(?:\.\d+)?\s*\)|-?\d+\.\d+)$/);
	});

	it("keeps the precision prefix and dither chunk the felt body depends on", () => {
		const shader = buildFeltFragmentShader();
		expect(shader).toContain("precision highp float;");
		expect(shader).toContain("float bayer4(");
	});
});
