// position.xy spans -1..1 (PlaneGeometry(2,2), no camera involved — this
// bypasses the view/projection matrices entirely and writes clip space
// directly), so uv = position.xy * 0.5 + 0.5 gives 0..1 without a camera.
varying vec2 vUv;
void main() {
	vUv = position.xy * 0.5 + 0.5;
	gl_Position = vec4(position.xy, 0.0, 1.0);
}
