<!-- The actual ripple mesh + shader — must be a child of <Canvas> (useTask
     requires it), so ShaderBackground.svelte just wraps this in <Canvas>. -->
<script lang="ts">
	import { T, useTask, useThrelte } from "@threlte/core";
	import * as THREE from "three";
	import { RippleField, MAX_RIPPLES } from "$lib/webgl/rippleField";

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

	// position.xy spans -1..1 (PlaneGeometry(2,2), no camera involved — this
	// bypasses the view/projection matrices entirely and writes clip space
	// directly), so uv = position.xy * 0.5 + 0.5 gives 0..1 without a camera.
	const vertexShader = `
		varying vec2 vUv;
		void main() {
			vUv = position.xy * 0.5 + 0.5;
			gl_Position = vec4(position.xy, 0.0, 1.0);
		}
	`;

	// Ring test + a screen-space 4x4 ordered (Bayer) dither, matching the
	// flat, two-tone, no-alpha-blending dither language the rest of the app
	// already uses (app.css's .dither-2/4/8) instead of a smooth gradient ring.
	const fragmentShader = `
		// highp, not mediump: gl_FragCoord is in the 100s-1000s of pixels, and
		// mediump's limited mantissa loses integer accuracy at that magnitude,
		// which corrupted the mod(floor(fragCoord/cell)) dither-cell math into
		// effectively random per-pixel noise across the whole canvas instead
		// of a clean periodic Bayer tile.
		precision highp float;
		varying vec2 vUv;
		uniform float uAspect;
		uniform vec2 uResolution;
		uniform float uTime;
		uniform vec3 uBase;
		uniform vec3 uHint;
		uniform float uCentersX[${MAX_RIPPLES}];
		uniform float uCentersY[${MAX_RIPPLES}];
		uniform float uRadii[${MAX_RIPPLES}];
		uniform float uIntensities[${MAX_RIPPLES}];
		uniform float uShapes[${MAX_RIPPLES}];

		// Ring half-width at birth/death (thin, still just a dithered line)
		// vs. at peak-of-life (fully solid ink blot, dithered only at its
		// inner/outer rim). uShapes[i] (0..1, peaked mid-lifetime) interpolates
		// between the two every frame.
		const float BASE_HALF_WIDTH = 0.045;
		const float BLOB_EXTRA_HALF_WIDTH = 0.07;
		const float EDGE_SOFT = 0.03;
		const float MERGE_K = 0.35;

		// How far the outline bulges off a perfect circle, and how much the
		// line's thickness itself varies around that outline — both as a
		// fraction of the ripple's own size, so small and large ripples wobble
		// proportionally instead of the same blob looking chunky when tiny.
		const float RADIUS_WOBBLE = 0.35;
		const float THICKNESS_WOBBLE = 0.6;

		// Cheap organic wobble: three sine harmonics at incommensurate
		// frequencies around the angle, seeded per-ripple so blobs don't all
		// wobble in lockstep, and rotating slowly with uTime so the outline
		// writhes instead of holding a static bulge pattern.
		float wobble(float angle, float seed) {
			return sin(angle * 3.0 + seed) * 0.5
				+ sin(angle * 5.0 + seed * 1.7 + 1.3) * 0.3
				+ sin(angle * 7.0 + seed * 2.3 + 2.6) * 0.2;
		}

		// Pixel grid the whole effect is quantized to — both the ripple
		// geometry AND the dither matrix sample this same grid (matching the
		// codrops dithering-shader approach of tying the Bayer lookup to the
		// same pixelation cell as the pixelated coordinates), so there's one
		// consistent block size instead of two competing scales fighting
		// each other.
		const float GRID_COLUMNS = 180.0;

		// Polynomial smooth-min (Quilez) — blends two values instead of hard
		// min/max, so overlapping ripples thicken into each other like SDF
		// metaballs merging rather than one ring just occluding the other.
		float smin(float a, float b, float k) {
			float h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);
			return mix(b, a, h) - k * h * (1.0 - h);
		}

		float smax(float a, float b, float k) {
			return -smin(-a, -b, k);
		}

		float bayerValue(vec2 fragCoord, vec2 cellSize) {
			vec2 cell = mod(floor(fragCoord / cellSize), 4.0);
			int index = int(cell.x) + int(cell.y) * 4;
			float bayer[16];
			bayer[0]=0.0; bayer[1]=8.0; bayer[2]=2.0; bayer[3]=10.0;
			bayer[4]=12.0; bayer[5]=4.0; bayer[6]=14.0; bayer[7]=6.0;
			bayer[8]=3.0; bayer[9]=11.0; bayer[10]=1.0; bayer[11]=9.0;
			bayer[12]=15.0; bayer[13]=7.0; bayer[14]=13.0; bayer[15]=5.0;
			for (int i = 0; i < 16; i++) {
				if (i == index) return bayer[i] / 16.0;
			}
			return 0.0;
		}

		void main() {
			float strength = 0.0;

			// Square cells sized off the horizontal resolution only, so the
			// grid stays square regardless of aspect — this is what makes the
			// ripple geometry itself blocky pixel art, not just an
			// anti-aliased curve with dither painted over it.
			vec2 cellSize = vec2(uResolution.x / GRID_COLUMNS);
			vec2 pixelUv = (floor(gl_FragCoord.xy / cellSize) * cellSize + cellSize * 0.5) / uResolution;

			for (int i = 0; i < ${MAX_RIPPLES}; i++) {
				vec2 center = vec2(uCentersX[i], uCentersY[i]);
				vec2 d = (pixelUv - center) * vec2(uAspect, 1.0);
				float dist = length(d);
				float radius = uRadii[i];

				// Per-ripple seed from its (fixed) center, so each ripple gets
				// its own bulge pattern instead of every one wobbling in sync.
				float seed = fract(sin(dot(center, vec2(12.9898, 78.233))) * 43758.5453) * 6.2831853;
				float angle = atan(d.y, d.x);
				float radiusNoise = wobble(angle, seed + uTime * 0.6);
				float thicknessNoise = wobble(angle * 1.3 + 10.0, seed * 1.9 - uTime * 0.4);

				float wobbleRadius = radius + radiusNoise * RADIUS_WOBBLE * max(radius, 0.05);
				float halfWidth = (BASE_HALF_WIDTH + uShapes[i] * BLOB_EXTRA_HALF_WIDTH)
					* max(0.15, 1.0 + thicknessNoise * THICKNESS_WOBBLE);
				float d2 = abs(dist - wobbleRadius);
				float band = (1.0 - smoothstep(halfWidth - EDGE_SOFT, halfWidth, d2)) * uIntensities[i];

				// Guard: smooth min/max overshoots near-equal inputs by up to
				// k/4 even at (0,0) — without this every empty patch of screen
				// got nudged to a non-zero strength, dithering the whole
				// background instead of just where ripples actually reach.
				if (strength > 0.001 && band > 0.001) {
					strength = smax(strength, band, MERGE_K);
				} else {
					strength = max(strength, band);
				}
			}

			// step(edge, x) fires on x >= edge, and the Bayer matrix includes a
			// 0.0 cell — without the epsilon, that one cell in 16 draws the hint
			// color even at strength 0.0, dithering the ENTIRE background at
			// every idle moment instead of only inside a ripple's ring.
			float threshold = bayerValue(gl_FragCoord.xy, cellSize) + 0.0001;
			float draw = step(threshold, strength);
			gl_FragColor = vec4(mix(uBase, uHint, draw), 1.0);
		}
	`;
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
