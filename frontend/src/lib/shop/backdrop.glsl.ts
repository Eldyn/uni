/**
 * @file backdrop.glsl.ts
 * @brief Fragment source for the Shop and Wardrobe backdrop, kept as a string
 * because GLSL has no module system and the component compiles it directly.
 */

import bayerDither from "$lib/shaders/chunks/bayerDither.glsl?raw";

/** Square cells across the canvas width. Equal to the home ripple grid. */
export const BACKDROP_GRID_COLUMNS = 180;
/** The dither pattern moves one cell to the side per step. */
export const BACKDROP_SHIFT_STEP_S = 2;
/** At most this share of the cells shows the darker home color. */
export const BACKDROP_MAX_SHADE = 0.5;

/**
 * A flat field of the home hint color. An ordered (Bayer) dither replaces a
 * uShade share of the cells with the home base color; uShade is the middle
 * dark share, and the crossing bands move around it. The picture only gets
 * darker and every cell is exactly one of the two palette colors.
 * WebGL2 / GLSL ES 3.00. Alpha is always 1.0.
 */
export const BACKDROP_FRAGMENT_SOURCE = `#version 300 es
precision highp float;

uniform float uTime;
uniform vec2 uResolution;
uniform vec3 uBase;
uniform vec3 uHint;
uniform float uShade;

out vec4 fragColor;

const float GRID_COLUMNS = ${BACKDROP_GRID_COLUMNS.toFixed(1)};
const float SHIFT_STEP_S = ${BACKDROP_SHIFT_STEP_S.toFixed(1)};
const float MAX_SHADE = ${BACKDROP_MAX_SHADE.toFixed(2)};
const float TAU = 6.28318530718;
const float BAND_A_PERIOD_S = 32.0;
const float BAND_B_PERIOD_S = 48.0;
const float BAND_SHADE = 0.35;

${bayerDither}

void main() {
	vec2 cellSize = vec2(max(uResolution.x, 1.0) / GRID_COLUMNS);
	vec2 cell = floor(gl_FragCoord.xy / cellSize);
	vec2 shift = vec2(floor(uTime / SHIFT_STEP_S), 0.0);
	vec2 uv = (cell * cellSize) / max(uResolution, vec2(1.0));
	float waveA = 0.5 + 0.5 * sin(TAU * ((uv.x + uv.y) * 1.8 - uTime / BAND_A_PERIOD_S));
	float waveB = 0.5 + 0.5 * sin(TAU * ((uv.x - uv.y) * 1.4 + uTime / BAND_B_PERIOD_S));
	float bands = smoothstep(0.50, 0.80, waveA) + 0.6 * smoothstep(0.50, 0.80, waveB);
	// INFO: the epsilon keeps the 0.0 Bayer cell off at uShade 0 (see home).
	float threshold = bayer4(cell + shift) + 0.0001;
	float shade = clamp(uShade + BAND_SHADE * (bands - 0.75), 0.0, MAX_SHADE);
	float dark = step(threshold, shade);
	fragColor = vec4(mix(uHint, uBase, dark), 1.0);
}
`;
