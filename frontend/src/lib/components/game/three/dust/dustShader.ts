/**
 * @file dustShader.ts
 * @brief Assembles the ambient dust vertex/fragment sources. Kept as pure
 * functions rather than inline in AmbientDust3D.svelte so a unit test can read
 * exactly the GLSL handed to THREE.ShaderMaterial — the component's Threlte
 * mock never sees a material's shader strings, which is how a non-compiling
 * literal once shipped.
 *
 * Neither source gets a precision prefix here: three.js already prepends its
 * own default, and both shaders declare the precision they need themselves
 * (highp in the vertex for the unbounded uTime clock, mediump in the
 * fragment). Prepending again used to give the fragment two explicit
 * `precision mediump float;` declarations.
 */

import dustVertexSource from "$lib/shaders/dust.vert.glsl?raw";
import dustFragmentSource from "$lib/shaders/dust.frag.glsl?raw";

export function buildDustVertexShader(): string {
	return dustVertexSource;
}

export function buildDustFragmentShader(): string {
	return dustFragmentSource;
}
