<!-- The Shop's backdrop: one WebGL2 canvas painting a dark purple field with
     slow diagonal energy streaks and drifting pixel dust. It sits behind the
     screen, ignores pointer events, and follows the screen's motion rules: the
     loop stops while the tab is hidden and never starts under reduced motion,
     which gets a single still frame instead. When WebGL2 is missing the CSS
     gradient layer underneath is the whole backdrop. -->
<script lang="ts">
	import { onDestroy, onMount } from "svelte";
	import { BACKDROP_FRAGMENT_SOURCE } from "$lib/shop/backdrop.glsl";

	// A full-screen triangle: the vertex shader writes clip space directly, so
	// the fragment shader can derive uv from gl_FragCoord alone.
	const VERTEX_SOURCE = `#version 300 es
in vec2 aPosition;
void main() {
	gl_Position = vec4(aPosition, 0.0, 1.0);
}
`;

	// The app accent (#c084fc) in case getComputedStyle cannot resolve --accent.
	const ACCENT_FALLBACK: [number, number, number] = [0.7529, 0.5176, 0.9882];
	const MAX_PIXEL_RATIO = 2;

	let { dim = 0 }: { dim?: number } = $props();

	let host: HTMLDivElement | undefined = $state();
	let ready = $state(false);

	let gl: WebGL2RenderingContext | null = null;
	let canvas: HTMLCanvasElement | null = null;
	let program: WebGLProgram | null = null;
	let uniforms: {
		time: WebGLUniformLocation | null;
		resolution: WebGLUniformLocation | null;
		accent: WebGLUniformLocation | null;
		dim: WebGLUniformLocation | null;
	} | null = null;
	let accent = ACCENT_FALLBACK;

	let rafId: number | null = null;
	let reducedMotion = false;
	let resizeObserver: ResizeObserver | null = null;

	function parseHexColour(value: string): [number, number, number] | null {
		const hex = value.trim().replace(/^#/, "");
		if (!/^[0-9a-fA-F]{6}$/.test(hex)) return null;
		return [
			parseInt(hex.slice(0, 2), 16) / 255,
			parseInt(hex.slice(2, 4), 16) / 255,
			parseInt(hex.slice(4, 6), 16) / 255
		];
	}

	function readAccent(): [number, number, number] {
		if (!host || typeof getComputedStyle !== "function") return ACCENT_FALLBACK;
		return parseHexColour(getComputedStyle(host).getPropertyValue("--accent")) ?? ACCENT_FALLBACK;
	}

	function compileShader(context: WebGL2RenderingContext, type: number, source: string) {
		const shader = context.createShader(type);
		if (!shader) throw new Error("shop backdrop: createShader failed");
		context.shaderSource(shader, source);
		context.compileShader(shader);
		if (!context.getShaderParameter(shader, context.COMPILE_STATUS)) {
			const log = context.getShaderInfoLog(shader);
			context.deleteShader(shader);
			throw new Error(`shop backdrop: shader failed to compile: ${log}`);
		}
		return shader;
	}

	function buildProgram(context: WebGL2RenderingContext): WebGLProgram {
		const vertex = compileShader(context, context.VERTEX_SHADER, VERTEX_SOURCE);
		const fragment = compileShader(context, context.FRAGMENT_SHADER, BACKDROP_FRAGMENT_SOURCE);
		const linked = context.createProgram();
		if (!linked) throw new Error("shop backdrop: createProgram failed");
		context.attachShader(linked, vertex);
		context.attachShader(linked, fragment);
		context.linkProgram(linked);
		context.deleteShader(vertex);
		context.deleteShader(fragment);
		if (!context.getProgramParameter(linked, context.LINK_STATUS)) {
			const log = context.getProgramInfoLog(linked);
			context.deleteProgram(linked);
			throw new Error(`shop backdrop: program failed to link: ${log}`);
		}
		return linked;
	}

	function configureGeometry(context: WebGL2RenderingContext, linked: WebGLProgram): void {
		const buffer = context.createBuffer();
		context.bindBuffer(context.ARRAY_BUFFER, buffer);
		context.bufferData(
			context.ARRAY_BUFFER,
			new Float32Array([-1, -1, 3, -1, -1, 3]),
			context.STATIC_DRAW
		);
		const position = context.getAttribLocation(linked, "aPosition");
		context.enableVertexAttribArray(position);
		context.vertexAttribPointer(position, 2, context.FLOAT, false, 0, 0);
	}

	function resize(context: WebGL2RenderingContext, target: HTMLCanvasElement): void {
		const ratio = Math.min(
			typeof window === "undefined" ? 1 : window.devicePixelRatio || 1,
			MAX_PIXEL_RATIO
		);
		const box = host?.getBoundingClientRect();
		const width = Math.max(1, Math.round((box?.width || 1) * ratio));
		const height = Math.max(1, Math.round((box?.height || 1) * ratio));
		if (target.width !== width) target.width = width;
		if (target.height !== height) target.height = height;
		context.viewport(0, 0, width, height);
	}

	function draw(timeMs: number): void {
		if (!gl || !program || !uniforms || !canvas) return;
		gl.useProgram(program);
		gl.uniform1f(uniforms.time, timeMs / 1000);
		gl.uniform2f(uniforms.resolution, canvas.width, canvas.height);
		gl.uniform3f(uniforms.accent, accent[0], accent[1], accent[2]);
		gl.uniform1f(uniforms.dim, dim);
		gl.drawArrays(gl.TRIANGLES, 0, 3);
	}

	function frame(timeMs: number): void {
		draw(timeMs);
		rafId = requestAnimationFrame(frame);
	}

	function start(): void {
		if (rafId !== null || !gl) return;
		if (reducedMotion) {
			draw(performance.now());
			return;
		}
		rafId = requestAnimationFrame(frame);
	}

	function stop(): void {
		if (rafId === null) return;
		cancelAnimationFrame(rafId);
		rafId = null;
	}

	function onVisibilityChange(): void {
		if (document.hidden) stop();
		else start();
	}

	function dispose(): void {
		stop();
		resizeObserver?.disconnect();
		resizeObserver = null;
		if (gl) {
			gl.getExtension("WEBGL_lose_context")?.loseContext();
		}
		gl = null;
		program = null;
		uniforms = null;
		canvas?.remove();
		canvas = null;
	}

	onMount(() => {
		if (typeof window === "undefined") return;
		reducedMotion = window.matchMedia?.("(prefers-reduced-motion: reduce)").matches ?? false;

		const target = document.createElement("canvas");
		const context = target.getContext("webgl2") as WebGL2RenderingContext | null;
		if (!context) return;

		try {
			const linked = buildProgram(context);
			context.useProgram(linked);
			configureGeometry(context, linked);
			gl = context;
			canvas = target;
			program = linked;
			accent = readAccent();
			uniforms = {
				time: context.getUniformLocation(linked, "uTime"),
				resolution: context.getUniformLocation(linked, "uResolution"),
				accent: context.getUniformLocation(linked, "uAccent"),
				dim: context.getUniformLocation(linked, "uDim")
			};
			resize(context, target);
			host?.appendChild(target);
			ready = true;
		} catch {
			context.getExtension("WEBGL_lose_context")?.loseContext();
			return;
		}

		if (typeof ResizeObserver !== "undefined" && host) {
			resizeObserver = new ResizeObserver(() => resize(context, target));
			resizeObserver.observe(host);
		}

		document.addEventListener("visibilitychange", onVisibilityChange);
		start();
	});

	onDestroy(dispose);
</script>

<div class="shop-backdrop" data-webgl={ready} bind:this={host} aria-hidden="true"></div>

<style>
	.shop-backdrop {
		position: absolute;
		inset: 0;
		z-index: 0;
		overflow: hidden;
		pointer-events: none;
		background: linear-gradient(160deg, #0b0614 0%, #1a0b2e 55%, #2a1350 100%);
	}

	.shop-backdrop :global(canvas) {
		display: block;
		width: 100%;
		height: 100%;
	}
</style>
