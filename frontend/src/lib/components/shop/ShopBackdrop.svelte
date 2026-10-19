<!-- The Shop's backdrop: one WebGL2 canvas painting the home screen colors as a
     calm field with two very slow diagonal bands and a few dust pixels that
     fade in place. It sits behind the screen, ignores pointer events, draws at
     most 20 frames per second, stops while the tab is hidden, and draws one
     still frame (no loop) under reduced motion or on a slow-update display.
     When WebGL2 is missing the CSS gradient layer underneath is the whole
     backdrop. -->
<script lang="ts">
	import { onDestroy, onMount } from "svelte";
	import { BACKDROP_FRAGMENT_SOURCE, BACKDROP_PIXEL_SIZE_PX } from "$lib/shop/backdrop.glsl";
	import {
		BACKDROP_STILL_TIME_S,
		prefersStillBackdrop,
		shouldDrawFrame
	} from "$lib/shop/backdropTiming";
	import {
		HOME_BACKGROUND_BASE_HEX,
		HOME_BACKGROUND_HINT_HEX,
		hexToCss,
		hexToRgb01
	} from "$lib/shaders/homePalette";

	// A full-screen triangle: the vertex shader writes clip space directly, so
	// the fragment shader can derive uv from gl_FragCoord alone.
	const VERTEX_SOURCE = `#version 300 es
in vec2 aPosition;
void main() {
	gl_Position = vec4(aPosition, 0.0, 1.0);
}
`;

	const MAX_PIXEL_RATIO = 2;
	const BASE_RGB = hexToRgb01(HOME_BACKGROUND_BASE_HEX);
	const HINT_RGB = hexToRgb01(HOME_BACKGROUND_HINT_HEX);
	const FALLBACK_BACKGROUND = `linear-gradient(160deg, ${hexToCss(HOME_BACKGROUND_BASE_HEX)} 0%, ${hexToCss(HOME_BACKGROUND_HINT_HEX)} 100%)`;

	let { dim = 0 }: { dim?: number } = $props();

	let host: HTMLDivElement | undefined = $state();
	let ready = $state(false);

	let gl: WebGL2RenderingContext | null = null;
	let canvas: HTMLCanvasElement | null = null;
	let program: WebGLProgram | null = null;
	let uniforms: {
		time: WebGLUniformLocation | null;
		resolution: WebGLUniformLocation | null;
		base: WebGLUniformLocation | null;
		hint: WebGLUniformLocation | null;
		pixelSize: WebGLUniformLocation | null;
		dim: WebGLUniformLocation | null;
	} | null = null;
	let pixelRatio = 1;

	let rafId: number | null = null;
	let stillFrame = false;
	let resizeObserver: ResizeObserver | null = null;

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
		pixelRatio = ratio;
		const box = host?.getBoundingClientRect();
		const width = Math.max(1, Math.round((box?.width || 1) * ratio));
		const height = Math.max(1, Math.round((box?.height || 1) * ratio));
		if (target.width !== width) target.width = width;
		if (target.height !== height) target.height = height;
		context.viewport(0, 0, width, height);
	}

	function draw(timeSeconds: number): void {
		if (!gl || !program || !uniforms || !canvas) return;
		gl.useProgram(program);
		gl.uniform1f(uniforms.time, timeSeconds);
		gl.uniform2f(uniforms.resolution, canvas.width, canvas.height);
		gl.uniform3f(uniforms.base, BASE_RGB[0], BASE_RGB[1], BASE_RGB[2]);
		gl.uniform3f(uniforms.hint, HINT_RGB[0], HINT_RGB[1], HINT_RGB[2]);
		gl.uniform1f(uniforms.pixelSize, BACKDROP_PIXEL_SIZE_PX * pixelRatio);
		gl.uniform1f(uniforms.dim, dim);
		gl.drawArrays(gl.TRIANGLES, 0, 3);
	}

	let lastDrawMs = Number.NEGATIVE_INFINITY;

	function frame(timeMs: number): void {
		if (shouldDrawFrame(timeMs, lastDrawMs)) {
			draw(timeMs / 1000);
			lastDrawMs = timeMs;
		}
		rafId = requestAnimationFrame(frame);
	}

	function start(): void {
		if (rafId !== null || !gl) return;
		if (stillFrame) {
			draw(BACKDROP_STILL_TIME_S);
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
		stillFrame = prefersStillBackdrop();

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
			uniforms = {
				time: context.getUniformLocation(linked, "uTime"),
				resolution: context.getUniformLocation(linked, "uResolution"),
				base: context.getUniformLocation(linked, "uBase"),
				hint: context.getUniformLocation(linked, "uHint"),
				pixelSize: context.getUniformLocation(linked, "uPixelSize"),
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

<div
	class="shop-backdrop"
	data-webgl={ready}
	bind:this={host}
	style:background={FALLBACK_BACKGROUND}
	aria-hidden="true"
></div>

<style>
	.shop-backdrop {
		position: absolute;
		inset: 0;
		z-index: 0;
		overflow: hidden;
		pointer-events: none;
	}

	.shop-backdrop :global(canvas) {
		display: block;
		width: 100%;
		height: 100%;
	}
</style>
