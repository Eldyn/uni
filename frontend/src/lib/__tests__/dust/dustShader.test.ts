// Compile-adjacent guard over the assembled dust GLSL. There is no GL context
// in vitest, so these are source-string assertions over the real imported
// `?raw` sources (via the assembly helper AmbientDust3D itself uses). The
// phase's Critical was a silent shader-compile failure that shipped because no
// test ever saw the assembled source.
import { describe, it, expect } from "vitest";
import {
	buildDustVertexShader,
	buildDustFragmentShader
} from "$components/game/three/dust/dustShader";

// GLSL ES 1.00 (three.js adds no #version directive) has no implicit int->float
// conversion: `const float X = 2;` is a compile error that blanks the draw.
const BARE_INT_FLOAT_CONST = /const\s+float\s+\w+\s*=\s*-?\d+\s*;/;
// Any build-time substitution token left unresolved.
const UNRESOLVED_TOKEN = /__[A-Z0-9_]+__/;
const MEDIUM_PRECISION = /precision\s+mediump\s+float;/g;

describe("buildDustFragmentShader", () => {
	it("fully resolves every build-time token", () => {
		expect(UNRESOLVED_TOKEN.test(buildDustFragmentShader())).toBe(false);
	});

	it("emits no bare-int float const initializer", () => {
		expect(BARE_INT_FLOAT_CONST.test(buildDustFragmentShader())).toBe(false);
	});

	it("declares its precision exactly once, with no duplicate prefix", () => {
		const matches = buildDustFragmentShader().match(MEDIUM_PRECISION) ?? [];
		expect(matches).toHaveLength(1);
	});
});

describe("buildDustVertexShader", () => {
	it("keeps the highp precision the drift/bob clock needs", () => {
		expect(buildDustVertexShader()).toContain("precision highp float;");
	});

	it("emits no bare-int float const initializer", () => {
		expect(BARE_INT_FLOAT_CONST.test(buildDustVertexShader())).toBe(false);
	});
});
