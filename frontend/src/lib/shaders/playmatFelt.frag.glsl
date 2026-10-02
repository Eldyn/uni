// The felt: the painted sheet multiplied by the shared dithered ripple
// colour (matRipple.glsl). When no ripple is active the colour collapses to
// the flat committed tint, identical to the old tint*map output.
uniform sampler2D uMap;

varying vec2 vUv;

void main() {
	vec4 texel = texture2D(uMap, vUv);
	gl_FragColor = vec4(texel.rgb * matRippleColor(vUv), texel.a);
	#include <colorspace_fragment>
}
