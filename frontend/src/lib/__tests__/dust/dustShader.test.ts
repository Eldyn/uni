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

	it("declares the continuous ripple-elapsed uniform the puff decay rides", () => {
		expect(buildDustVertexShader()).toContain("uniform float uRippleElapsed;");
	});

	it("sizes the wild puff in world units, not block-scaled ones", () => {
		const src = buildDustVertexShader();
		// The world-unit constants replaced the old per-art-pixel block ones
		// (which scaled the whole blow down to ~0.04 world units and made it
		// invisible).
		expect(src).toContain("DUST_WILD_PROXIMITY_HEIGHT");
		expect(src).toContain("DUST_WILD_LIFT");
		expect(src).toContain("DUST_WILD_PUSH");
		expect(src).not.toContain("DUST_WILD_LIFT_HEIGHT_BLOCKS");
		expect(src).not.toContain("DUST_WILD_LIFT_BLOCKS");
		// Eligibility is a bare world-unit comparison, never multiplied back
		// by uBlockWorldSize.
		expect(src).toMatch(/abs\(basePosition\.y\)\s*<=\s*DUST_WILD_PROXIMITY_HEIGHT/);
		// Tightened regression guard: the lift/push must never be scaled back
		// into block units. `toContain("DUST_WILD_LIFT")` alone was satisfied
		// by unrelated names like DUST_WILD_LIFT_SECONDS, so it could not
		// catch the original block-scaling bug that shrank the blow to
		// invisibility (one block ≈ 0.03 world units).
		expect(src).not.toMatch(/DUST_WILD_LIFT\s*\*\s*uBlockWorldSize/);
		expect(src).not.toMatch(/DUST_WILD_PUSH\s*\*\s*uBlockWorldSize/);
	});

	it("ramps the wild puff in and holds it, never decaying back toward the origin", () => {
		const src = buildDustVertexShader();
		// A fast ramp that stays at 1 after the front passes: no reverse means
		// no visible travel back toward the origin (the reported inward flow)
		// and no sustained push window.
		expect(src).toMatch(/smoothstep\(0\.0,\s*DUST_WILD_IMPULSE_RAMP,\s*sincePassed\)/);
		expect(src).toMatch(/DUST_WILD_LIFT\s*\*\s*impulse/);
		expect(src).toMatch(/DUST_WILD_PUSH\s*\*\s*impulse/);
		// The old decay window (and its cut-off) must be gone entirely.
		expect(src).not.toContain("DUST_WILD_LIFT_SECONDS");
		expect(src).not.toMatch(/1\.0\s*-\s*clamp\(sincePassed/);
	});

	it("measures the puff ramp from the continuous elapsed clock, not the step grid", () => {
		expect(buildDustVertexShader()).toMatch(
			/sincePassed\s*=\s*uRippleElapsed\s*-\s*arrivalStep\s*\*\s*DUST_STEP_SECONDS/
		);
	});
});
