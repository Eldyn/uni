/**
 * @file feltShader.ts
 * @brief Assembles the felt's fragment shader — an explicit precision prefix,
 * the shared Bayer-dither chunk, and playmatFelt.frag.glsl with its build-time
 * constant substitution. Kept as a pure function rather than inline in
 * Playmat3D.svelte so a unit test can read the emitted GLSL: the component's
 * Threlte mock only records material `color` props, so it never sees the
 * fragmentShader string (which is how a non-compiling literal once shipped).
 */

import bayerDither from "$lib/shaders/chunks/bayerDither.glsl?raw";
import playmatFeltFragmentSource from "$lib/shaders/playmatFelt.frag.glsl?raw";
import { WILD_FLASH_RADIUS_BLOCKS } from "./ripplePlan";

/** Token in playmatFelt.frag.glsl replaced with ripplePlan's constant. */
export const WILD_FLASH_RADIUS_PLACEHOLDER = "__WILD_FLASH_RADIUS_BLOCKS__";

/** three.js auto-prepends its own default precision, but an explicit highp
 *  here guarantees the pixel-grid math (block snapping, gl_FragCoord
 *  dithering) doesn't lose integer accuracy to a mediump default. */
export const FELT_PRECISION_PREFIX = "precision highp float;\n\n";

export function buildFeltFragmentShader(): string {
	return (FELT_PRECISION_PREFIX + bayerDither + playmatFeltFragmentSource).replaceAll(
		WILD_FLASH_RADIUS_PLACEHOLDER,
		String(WILD_FLASH_RADIUS_BLOCKS)
	);
}
