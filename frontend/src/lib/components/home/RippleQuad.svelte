<!-- The actual ripple mesh + shader — must be a child of <Canvas> (useTask
     requires it), so ShaderBackground.svelte just wraps this in <Canvas>. -->
<script lang="ts">
	import { T, useTask, useThrelte } from "@threlte/core";
	import * as THREE from "three";
	import { RippleField, MAX_RIPPLES } from "$lib/webgl/rippleField";
	import bayerDither from "$lib/shaders/chunks/bayerDither.glsl?raw";
	import rippleQuadVertex from "$lib/shaders/rippleQuad.vert.glsl?raw";
	import rippleQuadFragment from "$lib/shaders/rippleQuad.frag.glsl?raw";

	// Threlte's <Canvas> defaults the renderer to ACESFilmicToneMapping (a
	// "nicer PBR lighting" default) — that non-linearly compresses/darkens
	// raw near-black-to-midtone values, which is why the flat #16171d/#2c354d
	// hex colors were rendering visibly darker than their CSS counterparts.
	// This background is flat 2D UI, not a lit scene, so tone mapping is
	// disabled entirely to get exact hex colors out.
	const { renderer, size } = useThrelte();
	renderer.toneMapping = THREE.NoToneMapping;

	// THREE.Color's ColorManagement (on by default since r152) silently
	// converts an sRGB hex to linear space AT CONSTRUCTION TIME, before any
	// explicit .convertSRGBToLinear() call — so `new THREE.Color(hex)` was
	// already darkened once on its own, and manually converting it again
	// stacked a second conversion on top, crushing it toward black. Bypassing
	// THREE.Color entirely and building the raw 0..1 components by hand
	// avoids that implicit conversion, so what reaches the shader is exactly
	// the intended hex, unmodified.
	function hexToVec3(hex: number): THREE.Vector3 {
		return new THREE.Vector3(
			((hex >> 16) & 255) / 255,
			((hex >> 8) & 255) / 255,
			(hex & 255) / 255
		);
	}

	// Not measured live off the DOM — the halo it replaces was a fixed
	// constant too. Tuned to roughly where the logo sits in landing-grid.
	const CENTER_X = 0.5;
	const CENTER_Y = 0.32;

	const field = new RippleField(CENTER_X, CENTER_Y);

	// size is Threlte's own container-tracked (ResizeObserver-driven) canvas
	// size, not window.innerWidth/innerHeight — the canvas's actual box can
	// be narrower than the window (e.g. hero-zone shrinks when the sidebar
	// is open), so deriving aspect from the window squashed the ripples
	// whenever that box's aspect ratio diverged from the window's.
	function currentAspect(): number {
		const { width, height } = size.current;
		return height > 0 ? width / height : 1;
	}

	const uniforms = {
		uAspect: { value: currentAspect() },
		uResolution: { value: new THREE.Vector2(size.current.width, size.current.height) },
		uBase: { value: hexToVec3(0x16171d) },
		uHint: { value: hexToVec3(0x2c354d) },
		uTime: { value: 0 },
		uCentersX: { value: new Float32Array(MAX_RIPPLES) },
		uCentersY: { value: new Float32Array(MAX_RIPPLES) },
		uRadii: { value: new Float32Array(MAX_RIPPLES) },
		uIntensities: { value: new Float32Array(MAX_RIPPLES) },
		uShapes: { value: new Float32Array(MAX_RIPPLES) }
	};

	// Capped well below display refresh: the wave is meant to read as a
	// stepped pixel-art animation, not a smoothly interpolated one, and
	// skipping field.update/snapshot between steps is the actual resource
	// saving (the shader itself still runs every real frame, but on unchanged
	// uniforms it just redraws the same image).
	const SIM_FPS = 24;
	let clock = 0;
	let lastSimFrame = -1;
	useTask((delta) => {
		clock += delta;
		const simFrame = Math.floor(clock * SIM_FPS);
		if (simFrame === lastSimFrame) return;
		lastSimFrame = simFrame;

		const simTime = simFrame / SIM_FPS;
		field.update(simTime);
		const snap = field.snapshot(simTime);
		uniforms.uTime.value = simTime;
		uniforms.uCentersX.value.set(snap.centersX);
		uniforms.uCentersY.value.set(snap.centersY);
		uniforms.uRadii.value.set(snap.radii);
		uniforms.uIntensities.value.set(snap.intensities);
		uniforms.uShapes.value.set(snap.shapes);
		uniforms.uAspect.value = currentAspect();
		// gl_FragCoord is in device (framebuffer) pixels, not CSS pixels — must
		// scale by the renderer's pixel ratio or the grid math silently reads
		// the wrong resolution on any DPR != 1 screen.
		const dpr = renderer.getPixelRatio();
		uniforms.uResolution.value.set(size.current.width * dpr, size.current.height * dpr);
	});

	const vertexShader = rippleQuadVertex;

	// The fragment shader references the shared Bayer dither chunk's
	// bayer4() and needs MAX_RIPPLES baked into its array sizes/loop bound —
	// GLSL has no #include or preprocessor constant coming from TS, so both
	// are resolved here at load time instead of at shader-file authoring
	// time.
	const fragmentShader = (bayerDither + rippleQuadFragment).replaceAll(
		"__MAX_RIPPLES__",
		String(MAX_RIPPLES)
	);
</script>

<!-- frustumCulled=false: the vertex shader writes clip space directly,
     bypassing the camera transform entirely, so three.js's normal
     frustum test (run against the mesh's real transform vs. the default
     camera) can't be trusted to keep this plane visible — without this it
     was being culled and never drawn at all. -->
<T.Mesh frustumCulled={false}>
	<T.PlaneGeometry args={[2, 2]} />
	<T.ShaderMaterial
		{vertexShader}
		{fragmentShader}
		{uniforms}
		depthTest={false}
		depthWrite={false}
	/>
</T.Mesh>
