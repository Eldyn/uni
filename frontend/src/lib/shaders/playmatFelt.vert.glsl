// Ordinary MVP transform — unlike RippleQuad's full-screen quad, the felt is
// a real mesh in the 3D scene, so it needs the camera's projection instead
// of writing clip space directly. `position`/`uv`/`modelViewMatrix`/
// `projectionMatrix` are three.js's own auto-declared ShaderMaterial
// attributes/uniforms.
varying vec2 vUv;

void main() {
	vUv = uv;
	gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
}
