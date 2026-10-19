/**
 * @file backdrop.glsl.ts
 * @brief Fragment source for the shop shader backdrop, kept as a string because
 * GLSL has no module system and the component compiles it directly.
 */

/**
 * A dark purple field with slow diagonal energy streaks and drifting pixel dust.
 * WebGL2 / GLSL ES 3.00. The whole effect is quantised to a 3 px grid so it
 * reads as pixel art, matching the rest of the UI.
 */
export const BACKDROP_FRAGMENT_SOURCE = `#version 300 es
precision highp float;

uniform float uTime;
uniform vec2 uResolution;
uniform vec3 uAccent;
uniform float uDim;

out vec4 fragColor;

const float PIXEL_GRID = 3.0;

float hash(vec2 p) {
	return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

void main() {
	vec2 grid = floor(gl_FragCoord.xy / PIXEL_GRID);
	vec2 uv = grid * PIXEL_GRID / max(uResolution, vec2(1.0));

	// Dark purple field, lighter toward the top of the screen.
	vec3 deep = vec3(0.043, 0.020, 0.078);
	vec3 mid = vec3(0.114, 0.055, 0.208);
	vec3 colour = mix(deep, mid, uv.y * 0.75 + 0.15);

	// Slow diagonal energy streaks along the (x + y) axis.
	float diagonal = (uv.x + uv.y) * 3.0;
	float streak = pow(max(sin(diagonal * 3.14159 + uTime * 0.35), 0.0), 8.0);
	colour += uAccent * streak * 0.30;

	// A second, shallower streak set for depth.
	float cross = (uv.x * 0.6 - uv.y) * 4.0;
	float streak2 = pow(max(sin(cross * 3.14159 - uTime * 0.18), 0.0), 12.0);
	colour += uAccent * streak2 * 0.16;

	// Drifting pixel dust: sparse lit cells on a coarse scrolling grid, each
	// twinkling on its own phase.
	vec2 drift = floor(grid / 12.0) + vec2(uTime * 0.25, -uTime * 0.18);
	float seed = hash(drift);
	float lit = step(0.983, seed);
	float twinkle = 0.45 + 0.55 * sin(uTime * 1.7 + seed * 40.0);
	colour += uAccent * lit * twinkle * 0.85;

	colour *= 1.0 - clamp(uDim, 0.0, 1.0) * 0.55;

	fragColor = vec4(colour, 1.0);
}
`;
