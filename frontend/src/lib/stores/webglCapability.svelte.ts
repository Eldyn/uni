/** Gates the shader home-screen background: true only once the app has confirmed
 *  the device has real GPU acceleration (not a software rasterizer) and the
 *  user hasn't asked for reduced motion. Either gate failing falls back to
 *  the static bg_main.png + dither-halo, unchanged. Hardware acceleration is
 *  checked once per session; reduced-motion stays live so toggling the OS
 *  setting swaps the background immediately without a reload. */

function detectHardwareAcceleration(): boolean {
	if (typeof document === "undefined") return false;

	let gl: WebGLRenderingContext | WebGL2RenderingContext | null = null;
	try {
		const canvas = document.createElement("canvas");
		gl = (canvas.getContext("webgl2", { failIfMajorPerformanceCaveat: true }) ??
			canvas.getContext("webgl", { failIfMajorPerformanceCaveat: true })) as
			| WebGLRenderingContext
			| WebGL2RenderingContext
			| null;
	} catch {
		return false;
	}
	if (!gl) return false;

	const debugInfo = gl.getExtension("WEBGL_debug_renderer_info");
	const renderer = debugInfo
		? (gl.getParameter(debugInfo.UNMASKED_RENDERER_WEBGL) as string)
		: "";
	return !/swiftshader|llvmpipe|software/i.test(renderer);
}

class WebglCapability {
	hardwareAccelerated = $state(typeof window !== "undefined" && detectHardwareAcceleration());
	reducedMotion = $state(
		typeof window !== "undefined" && window.matchMedia("(prefers-reduced-motion: reduce)").matches
	);

	constructor() {
		if (typeof window === "undefined") return;
		window.matchMedia("(prefers-reduced-motion: reduce)").addEventListener("change", (e) => {
			this.reducedMotion = e.matches;
		});
	}

	get shaderBackgroundEnabled(): boolean {
		return this.hardwareAccelerated && !this.reducedMotion;
	}
}

export const storeWebglCapability = new WebglCapability();
