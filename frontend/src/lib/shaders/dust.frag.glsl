// Flat, unlit gray motes — vAlpha (dust.vert.glsl) already carries both the
// per-instance base alpha and the lifecycle fade-in/out, so this just paints
// the one colour through it.

// DUST_COLOR is authored directly as the output-space bytes of #9aa0a6, so
// there is deliberately no #include <colorspace_fragment> here: adding it
// would apply linear->sRGB output encoding a second time and render the motes
// lighter than the intended colour. toneMapped:false on the material already
// disables tonemapping.
precision mediump float;

const vec3 DUST_COLOR = vec3(0.6039, 0.6275, 0.6510); // #9aa0a6

varying float vAlpha;

void main() {
	gl_FragColor = vec4(DUST_COLOR, vAlpha);
}
