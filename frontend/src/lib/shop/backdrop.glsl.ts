/**
 * @file backdrop.glsl.ts
 * @brief Fragment source for the shop shader backdrop, kept as a string because
 * GLSL has no module system and the component compiles it directly.
 */

/** One backdrop pixel, in CSS pixels. The component scales it by the DPR. */
export const BACKDROP_PIXEL_SIZE_PX = 4;
/** Slow diagonal bands. Both periods are above 20 seconds on purpose. */
export const BACKDROP_BAND_A_PERIOD_S = 32;
export const BACKDROP_BAND_B_PERIOD_S = 48;
/** The picture never gets brighter than the hint color (lift 1). */
export const BACKDROP_MAX_LIFT = 1;

/**
 * A flat home-screen field with a faint top gradient, two very slow diagonal
 * bands and a few dust pixels that fade in place. The picture is always
 * mix(uBase, uHint, lift), so it never leaves the home screen palette.
 * WebGL2 / GLSL ES 3.00. Everything is quantized to uPixelSize.
 *
 * INFO: no hash input contains time. A dust pixel keeps its place and only its
 * glow changes. The old shader added time to the hash input, which re-rolled
 * every cell on every frame and caused the flicker.
 */
export const BACKDROP_FRAGMENT_SOURCE = `#version 300 es
precision highp float;

uniform float uTime;
uniform vec2 uResolution;
uniform vec3 uBase;
uniform vec3 uHint;
uniform float uPixelSize;
uniform float uDim;

out vec4 fragColor;

const float TAU = 6.28318530718;
const float BAND_A_PERIOD_S = ${BACKDROP_BAND_A_PERIOD_S.toFixed(1)};
const float BAND_B_PERIOD_S = ${BACKDROP_BAND_B_PERIOD_S.toFixed(1)};
const float GRADIENT_LIFT = 0.35;
const float BAND_A_LIFT = 0.30;
const float BAND_B_LIFT = 0.18;
const float DUST_LIFT = 0.45;
const float MAX_LIFT = ${BACKDROP_MAX_LIFT.toFixed(2)};
const float LIFT_STEPS = 4.0;
const float DUST_BLOCK = 6.0;
const float DUST_DENSITY = 0.025;
const float DUST_PERIOD_MIN_S = 8.0;
const float DUST_PERIOD_SPAN_S = 8.0;

float hash(vec2 p) {
	return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

// Rounds a 0..1 value to LIFT_STEPS levels so the picture stays pixel art.
float stepped(float value) {
	return floor(value * LIFT_STEPS + 0.5) / LIFT_STEPS;
}

void main() {
	vec2 cell = floor(gl_FragCoord.xy / uPixelSize);
	vec2 uv = cell * uPixelSize / max(uResolution, vec2(1.0));

	// Faint gradient, a little lighter toward the top of the screen.
	float lift = stepped(uv.y) * GRADIENT_LIFT;

	// Two very slow diagonal bands that cross each other.
	float waveA = 0.5 + 0.5 * sin(TAU * ((uv.x + uv.y) * 0.8 - uTime / BAND_A_PERIOD_S));
	lift += stepped(smoothstep(0.6, 1.0, waveA)) * BAND_A_LIFT;
	float waveB = 0.5 + 0.5 * sin(TAU * ((uv.x - uv.y) * 0.6 + uTime / BAND_B_PERIOD_S));
	lift += stepped(smoothstep(0.7, 1.0, waveB)) * BAND_B_LIFT;

	// Dust: at most one pixel per block. The block id is an integer, so the
	// pixel never moves; only its glow cycles, each block on its own phase.
	vec2 block = floor(cell / DUST_BLOCK);
	vec2 local = cell - block * DUST_BLOCK;
	vec2 pick = floor(vec2(hash(block + vec2(3.1, 7.7)), hash(block + vec2(11.3, 5.9))) * DUST_BLOCK);
	float present = step(1.0 - DUST_DENSITY, hash(block));
	float isPick = all(equal(local, pick)) ? 1.0 : 0.0;
	float period = DUST_PERIOD_MIN_S + DUST_PERIOD_SPAN_S * hash(block + vec2(2.2, 8.8));
	float cycle = fract(uTime / period + hash(block + vec2(6.6, 1.1)));
	float glow = smoothstep(0.0, 0.3, cycle) * (1.0 - smoothstep(0.5, 0.8, cycle));
	lift += present * isPick * stepped(glow) * DUST_LIFT;

	lift = min(lift, MAX_LIFT);
	vec3 colour = mix(uBase, uHint, lift);
	colour *= 1.0 - clamp(uDim, 0.0, 1.0) * 0.55;

	fragColor = vec4(colour, 1.0);
}
`;
