import { describe, it, expect } from "vitest";
import {
	buildFeltFragmentShader,
	buildTurnLoopFragmentShader,
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

	it("takes the ripple colour from the shared chunk instead of inlining it", () => {
		const shader = buildFeltFragmentShader();
		expect(shader).toContain("vec3 matRippleColor(vec2 uv)");
		expect(shader).toContain("matRippleColor(vUv)");
		// the nine ripple uniforms are declared exactly once
		expect(shader.split("uniform float uRadius;").length - 1).toBe(1);
	});

	it("keeps the ripple expressions the felt always used", () => {
		const shader = buildFeltFragmentShader();
		expect(shader).toContain("bayer4(blockUv * uBlockCount)");
		expect(shader).toContain("const float FLASH_LIGHTEN = 0.65;");
		expect(shader).toContain("texel.rgb * matRippleColor(vUv)");
	});
});

describe("buildTurnLoopFragmentShader", () => {
	it("resolves the flash-radius placeholder and keeps the precision prefix", () => {
		const shader = buildTurnLoopFragmentShader();
		expect(shader).not.toContain(WILD_FLASH_RADIUS_PLACEHOLDER);
		expect(shader).toContain("precision highp float;");
	});

	it("reuses the felt's ripple colour and dither", () => {
		const shader = buildTurnLoopFragmentShader();
		expect(shader).toContain("float bayer4(");
		expect(shader).toContain("vec3 matRippleColor(vec2 uv)");
		expect(shader).toContain("matRippleColor(vUv)");
		expect(shader.split("uniform float uRadius;").length - 1).toBe(1);
	});

	it("declares every uniform the component feeds it", () => {
		const shader = buildTurnLoopFragmentShader();
		for (const name of [
			"uLoopCenter",
			"uLoopHalfSize",
			"uLoopCornerRadius",
			"uLoopLength",
			"uPitch",
			"uPhase",
			"uDirection",
			"uLighten",
			"uChevronLength",
			"uChevronHalfSpread",
			"uStroke",
			"uDashLength",
			"uDashPitch",
			"uDashHalfWidth",
			"uDashClearance"
		]) {
			expect(shader).toMatch(new RegExp(`uniform\\s+(float|vec2)\\s+${name};`));
		}
	});

	it("mirrors the TypeScript outline reference piece for piece", () => {
		const shader = buildTurnLoopFragmentShader();
		expect(shader).toContain("vec2 outlineParam(vec2 offset)");
		expect(shader).toContain("bottomLeftArcStart");
	});
});
